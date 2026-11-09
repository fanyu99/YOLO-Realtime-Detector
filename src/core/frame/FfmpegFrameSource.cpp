#include "FfmpegFrameSource.h"
#include "CpuFrameHandle.h"

extern "C" {
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

// 工具
namespace {
	// 解码错误码 -> 可读字符串
	QString decodeErrorCodeToString( DecodeErrorCode code ) {
		switch (code) {
		case DecodeErrorCode::NoError:			 return QStringLiteral( "NoError" );
		case DecodeErrorCode::InvalidUrl:		 return QStringLiteral( "InvalidUrl" );
		case DecodeErrorCode::InvalidConfig:	 return QStringLiteral( "InvalidConfig" );
		case DecodeErrorCode::OpenFailed:		 return QStringLiteral( "OpenFailed" );
		case DecodeErrorCode::CodecNotFound:	 return QStringLiteral( "CodecNotFound" );
		case DecodeErrorCode::StreamNotFound:	 return QStringLiteral( "StreamNotFound" );
		case DecodeErrorCode::StreamFailed:		 return QStringLiteral( "StreamFailed" );
		case DecodeErrorCode::DecoderInitFailed: return QStringLiteral( "DecoderInitFailed" );
		case DecodeErrorCode::DecodeFailed:		 return QStringLiteral( "DecodeFailed" );
		case DecodeErrorCode::ReadFailed:		 return QStringLiteral( "ReadFailed" );
		case DecodeErrorCode::ReadTimeout:		 return QStringLiteral( "ReadTimeout" );
		case DecodeErrorCode::Other:			 return QStringLiteral( "Other" );
		}
		return QStringLiteral( "Unknown" );
	}

	// 解码错误 -> 字符串
	QString fromDecodeError( const DecodeError& error ) {
		return QStringLiteral( "[FfmpegFrameSource]: %1 (ErrorCode= %2)" )
			.arg( error.errorMessage, decodeErrorCodeToString( error.errorCode ) );
	}

	// 读取状态 -> 字符串
	QString readStatusToString( ReadStatus status ) {
		switch (status) {
		case ReadStatus::FrameReady: return QStringLiteral( "FrameReady" );
		case ReadStatus::EndOfFile:	 return QStringLiteral( "EndOfFile" );
		case ReadStatus::Paused:	 return QStringLiteral( "Paused" );
		case ReadStatus::Stopped:	 return QStringLiteral( "Stopped" );
		case ReadStatus::Error:		 return QStringLiteral( "Error" );
		}
		return QStringLiteral( "Unknown" );
	}

	// FFmpeg 像素格式 -> 字符串
	QString pixelFormatToString( int format ) {
		if (const char* name = av_get_pix_fmt_name( static_cast<AVPixelFormat>(format) ))
			return QString::fromUtf8( name );
		return QStringLiteral( "unknown(%1)" ).arg( format );
	}

	// 复用的像素格式转换上下文：分辨率或像素格式变化时自动重建
	class SwsContextCache {
	public:
		SwsContextCache() = default;
		~SwsContextCache() {
			if (m_context)
				sws_freeContext( m_context );
		}
		SwsContextCache( const SwsContextCache& ) = delete;
		SwsContextCache& operator=( const SwsContextCache& ) = delete;

		SwsContext* get( int width, int height, AVPixelFormat srcFormat, AVPixelFormat dstFormat ) {
			m_context = sws_getCachedContext(
				m_context,
				width, height, srcFormat,
				width, height, dstFormat,
				SWS_BILINEAR, nullptr, nullptr, nullptr );
			return m_context;
		}

	private:
		SwsContext* m_context{ nullptr };
	};

	// 解码出的 AVFrame -> cv::Mat（BGR 三通道）；失败返回空 Mat
	cv::Mat avFrameToMat( const AVFrame* frame ) {
		if (!frame || frame->width <= 0 || frame->height <= 0 || !frame->data[0])
			return cv::Mat();

		const auto sourceFormat = static_cast<AVPixelFormat>(frame->format);
		const AVPixFmtDescriptor* descriptor = av_pix_fmt_desc_get( sourceFormat );
		// 硬解帧（CUDA / D3D11 等）没有可直接读取的像素数据，需先 av_hwframe_transfer_data
		if (!descriptor || (descriptor->flags & AV_PIX_FMT_FLAG_HWACCEL))
			return cv::Mat();

		thread_local SwsContextCache swsCache;
		SwsContext* sws = swsCache.get( frame->width, frame->height, sourceFormat, AV_PIX_FMT_BGR24 );
		if (!sws)
			return cv::Mat();

		cv::Mat mat( frame->height, frame->width, CV_8UC3 );
		if (mat.empty())
			return mat;

		uint8_t* destinationData[4]{ mat.data, nullptr, nullptr, nullptr };
		const int destinationLinesize[4]{ static_cast<int>(mat.step), 0, 0, 0 };
		const int scaled = sws_scale(
			sws,
			frame->data, frame->linesize, 0, frame->height,
			destinationData, destinationLinesize );
		if (scaled <= 0)
			return cv::Mat();

		return mat;
	}
};

FfmpegFrameSource::~FfmpegFrameSource() {
	close();
}

FrameSourceType FfmpegFrameSource::type() const {
	return FrameSourceType::VideoFile;
}

bool FfmpegFrameSource::isOpened() const {
	return m_decoder.isOpened();
}

bool FfmpegFrameSource::isTotalFramesEstimated() const {
	return m_decoder.getStreamInfo().totalFramesEstimated;
}
size_t FfmpegFrameSource::totalFrames() const {
	const qint64 total = m_decoder.getStreamInfo().totalFrames;
	return total > 0 ? static_cast<size_t>(total) : 0;
}


double FfmpegFrameSource::fps() const {
	const StreamInfo info = m_decoder.getStreamInfo();
	if (info.frameRate.num <= 0 || info.frameRate.den <= 0)
		return 0.0;
	return av_q2d( info.frameRate );
}

cv::Size FfmpegFrameSource::frameSize() const {
	const StreamInfo info = m_decoder.getStreamInfo();
	if (info.width <= 0 || info.height <= 0)
		return cv::Size();
	return cv::Size( info.width, info.height );
}

QString FfmpegFrameSource::displayName() const {
	// 设备源优先展示友好名（如 "ASUS FHD webcam"），而不是 "video=..." 输入串
	if (m_config.deviceDisplayName && !m_config.deviceDisplayName->isEmpty())
		return *m_config.deviceDisplayName;
	if (m_config.isDevice())
		return *m_config.deviceInput;
	return m_url.isLocalFile() ? m_url.toLocalFile() : m_url.toString();
}

QString FfmpegFrameSource::lastError() const {
	if (!m_error.isEmpty()) {
		return m_error;
	}
	const DecodeError error = m_decoder.getLastError();
	if (error.errorCode == DecodeErrorCode::NoError)
		return QString();
	return fromDecodeError( error );
}

// 打开
bool FfmpegFrameSource::open() {
	m_error.clear();
	if (m_decoder.isOpened())
		return true;
	m_decoder.setConfig( m_config );
	// 如果使用GPU，为解码器设置硬件设备上下文
	if (m_config.useGpuDecode) {
		m_decoder.setHwDevice( CudaDevice::instance().borrowHwDevice() );
	}
	if (!m_decoder.open()) {
		m_error = fromDecodeError( m_decoder.getLastError() );
		return false;
	}
	return true;
}

// 获取帧
FrameHandlePtr FfmpegFrameSource::getFrame() {

	// 没有打开，尝试打开
	if (!isOpened()) {
		// 打开失败返回空指针
		if (!open()) {
			m_error = fromDecodeError( m_decoder.getLastError() );
			return nullptr;
		}
	}
	// 读取下一帧
	ReadStatus status = m_decoder.readNextFrame();
	// 	出现错误/EOF/停止，返回空指针
	if (status == ReadStatus::Error) {
		const DecodeError error = m_decoder.getLastError();
		if (error.errorCode != DecodeErrorCode::NoError || !error.errorMessage.isEmpty()) {
			m_error = fromDecodeError( m_decoder.getLastError() );
		}
		return nullptr;
	}
	if (status != ReadStatus::FrameReady) {
		return nullptr;
	}
	const cv::Mat mat = avFrameToMat( m_decoder.getCurrentFrame() );
	if (mat.empty()) {
		m_error = QStringLiteral( "[FfmpegFrameSource]: 图像格式转换失败" );
		return nullptr;
	}
	// 解码器计数从 1 起，句柄帧序号统一从 0 起
	return std::make_shared<CpuFrameHandle>( mat, m_decoder.decodeIndex() - 1 );
}
// 关闭
void FfmpegFrameSource::close() {
	m_error.clear();
	m_url.clear();
	m_decoder.close();
}
// 构造函数
FfmpegFrameSource::FfmpegFrameSource( const DecodeConfig& config, MemoryType memoryType ) : m_decoder( config ), m_config( config ), m_url( config.url ), m_memoryType( memoryType ) {
}
// 创建本地文件帧源（视频文件）
FrameSourcePtr FfmpegFrameSource::forLocalFile( QStringView filePath, MemoryType memoryType ) {
	if (filePath.isEmpty())
		return nullptr;
	QUrl url = QUrl::fromLocalFile( filePath.toString() );
	DecodeConfig config;
	if (memoryType == MemoryType::GPU
		) {
		config.useGpuDecode = true;
	}
	config.url = url;
	return FrameSourcePtr( new FfmpegFrameSource( config, memoryType ) );
}

// 创建设备帧源（摄像头/相机）
FrameSourcePtr FfmpegFrameSource::forDevice( const FfmpegDeviceInfo& device, const QString& formatName, MemoryType memoryType ) {
	if (device.name.isEmpty() && device.originalId.isEmpty())
		return nullptr;
	// 格式为空时回退到平台默认格式
	const QString fmt = formatName.isEmpty() ? FfmpegDecoder::defaultVideoInputFormat() : formatName;
	DecodeConfig config;
	// 如果是GPU，设置配置GPU解码
	if (memoryType == MemoryType::GPU) {
		config.useGpuDecode = true;
	}
	// 设备输入串：友好名优先
	config.deviceInput = FfmpegDecoder::deviceInput( device, fmt );
	if (!config.isDevice())
		return nullptr;
	// dshow 兜底：友好名打不开时用 PNP 路径重试
	const QString fallback = FfmpegDecoder::deviceFallbackInput( device, fmt );
	if (!fallback.isEmpty())
		config.deviceFallbackInput = fallback;
	config.deviceDisplayName = device.name;
	config.inputFormat = fmt;
	return FrameSourcePtr( new FfmpegFrameSource( config, memoryType ) );
}

// 按设备友好名（或原始标识）查找并创建帧源；名字写错也能模糊匹配
FrameSourcePtr FfmpegFrameSource::forDevice( const QString& nameOrId, const QString& formatName, MemoryType memoryType ) {
	const std::optional<FfmpegDeviceInfo> device = FfmpegDecoder::findVideoDevice( nameOrId, formatName );
	if (!device)
		return nullptr;
	return forDevice( *device, formatName, memoryType );
}

// 枚举 video 类型输入设备（转发 FfmpegDecoder 的列举方法）
FfmpegDeviceInfoList FfmpegFrameSource::enumerateVideoDevices( const QString& formatName ) {
	return FfmpegDecoder::enumerateVideoDevices( formatName );
}

// 当前平台的默认视频采集输入格式
QString FfmpegFrameSource::defaultVideoInputFormat() {
	return FfmpegDecoder::defaultVideoInputFormat();
}