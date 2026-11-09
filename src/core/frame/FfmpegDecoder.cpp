#include "FfmpegDecoder.h"
#include <QFile>
#include <QElapsedTimer>
#include <algorithm>
#include "Logger.h"
FfmpegDecoder::FfmpegDecoder( const DecodeConfig& config )
	: m_config( config )
	, m_interruptContex( config.readTimeoutMs ) {
}

FfmpegDecoder::~FfmpegDecoder() {
	close();
}

QUrl FfmpegDecoder::getUrl() const {
	return m_config.url;
}

// ====== 采集设备枚举 ======

// 当前平台的默认视频采集输入格式
QString FfmpegDecoder::defaultVideoInputFormat() {
#if defined(Q_OS_WIN)
	return QStringLiteral( "dshow" );
#elif defined(Q_OS_MACOS)
	return QStringLiteral( "avfoundation" );
#elif defined(Q_OS_LINUX)
	return QStringLiteral( "v4l2" );
#else
	return QString();
#endif
}

// 枚举指定输入格式下的 video 类型输入设备
FfmpegDeviceInfoList FfmpegDecoder::enumerateVideoDevices( const QString& formatName ) {
	// 注册所有设备（1次）
	static std::once_flag registerOnce;
	std::call_once( registerOnce, [ ] { avdevice_register_all(); } );

	// 获取格式
	const QString fmt = formatName.isEmpty() ? defaultVideoInputFormat() : formatName;
	if (fmt.isEmpty())
		return {};
	const AVInputFormat* inputFormat = av_find_input_format( fmt.toUtf8().constData() );
	if (!inputFormat) {
		Logger::instance()->warning( QStringLiteral( "[FfmpegDecoder]: 无法找到输入格式: %1，枚举设备失败" ).arg( fmt ) );
		return {};
	}

	// 获取设备列表
	AVDeviceInfoList* deviceList = nullptr;
	// 返回值为枚举到的设备数，负数表示错误
	const int count = avdevice_list_input_sources( inputFormat, nullptr, nullptr, &deviceList );
	if (count < 0 || !deviceList) {
		Logger::instance()->warning( QStringLiteral( "[FfmpegDecoder]: 枚举设备失败（%1）: %2" ).arg( fmt, FfmpegDecode::ffmepegError( count ) ) );
		return {};
	}

	// 获取设备信息
	FfmpegDeviceInfoList devices;
	devices.reserve( count );
	for (int i = 0; i < deviceList->nb_devices; ++i) {
		const AVDeviceInfo* device = deviceList->devices[i];
		if (!device || !device->media_types)
			continue;
		// 仅保留 video 类型设备（跳过纯音频设备）
		const bool hasVideo = std::any_of( device->media_types, device->media_types + device->nb_media_types,
			[ ]( const AVMediaType type ) { return type == AVMEDIA_TYPE_VIDEO; } );
		if (!hasVideo)
			continue;
		FfmpegDeviceInfo info;
		// dshow 的 device_name 是 PNP 路径（@device_pnp_\\?\usb#vid_...），
		// device_description 才是用户看到的友好名（如 "ASUS FHD webcam"）。
		// 统一用友好名作为 name，原始标识留作兜底。
		info.originalId = QString::fromUtf8( device->device_name );
		info.name = device->device_description && *device->device_description
			? QString::fromUtf8( device->device_description )
			: info.originalId;
		info.isDefault = (i == deviceList->default_device);
		devices.append( info );
	}
	avdevice_free_list_devices( &deviceList );	  // 释放内存

	Logger::instance()->info( QStringLiteral( "[FfmpegDecoder]: 枚举 %1 设备完成，共 %2 个 video 设备" ).arg( fmt ).arg( devices.size() ) );
	return devices;
}

// 根据友好名/原始标识查找设备
std::optional<FfmpegDeviceInfo> FfmpegDecoder::findVideoDevice( const QString& nameOrId, const QString& formatName ) {
	const QString target = nameOrId.trimmed();
	if (target.isEmpty())
		return std::nullopt;
	const FfmpegDeviceInfoList devices = enumerateVideoDevices( formatName );

	// 1.精确匹配（友好名 / 原始标识）
	for (const FfmpegDeviceInfo& device : devices) {
		if (device.name.compare( target, Qt::CaseInsensitive ) == 0
			|| device.originalId.compare( target, Qt::CaseInsensitive ) == 0)
			return device;
	}
	// 2.子串匹配
	for (const FfmpegDeviceInfo& device : devices) {
		if (device.name.contains( target, Qt::CaseInsensitive )
			|| device.originalId.contains( target, Qt::CaseInsensitive ))
			return device;
	}
	// 3.如果输入的设备名带有前缀
	for (const FfmpegDeviceInfo& device : devices) {
		if (!device.name.isEmpty() && target.contains( device.name, Qt::CaseInsensitive ))
			return device;
	}
	return std::nullopt;
}

// 由设备信息生成 ffmpeg 输入串（原样字符串，不做 URL 编码）
QString FfmpegDecoder::deviceInput( const FfmpegDeviceInfo& device, const QString& formatName ) {
	const QString fmt = formatName.isEmpty() ? defaultVideoInputFormat() : formatName;
	if (device.name.isEmpty() && device.originalId.isEmpty())
		return QString();
	if (fmt == QStringLiteral( "dshow" )) {
		// dshow 用友好名打开："video=ASUS FHD webcam"
		QString name = device.name.isEmpty() ? device.originalId : device.name;
		name.replace( QLatin1Char( '"' ), QChar( 0x201C ) );
		return QStringLiteral( "video=%1" ).arg( name );
	}
	if (fmt == QStringLiteral( "avfoundation" )) {
		// avfoundation 的 device_name 是 "<video_idx>.<audio_idx>"，只取视频部分
		const QString raw = device.originalId.isEmpty() ? device.name : device.originalId;
		const int colon = raw.indexOf( QLatin1Char( ':' ) );
		return colon >= 0 ? raw.left( colon ) : raw;
	}
	// v4l2 及其他格式：设备路径本身
	return device.originalId.isEmpty() ? device.name : device.originalId;
}

// 打开失败时的兜底输入串：dshow 回退到 PNP 路径（对重名友好名更可靠）
QString FfmpegDecoder::deviceFallbackInput( const FfmpegDeviceInfo& device, const QString& formatName ) {
	const QString fmt = formatName.isEmpty() ? defaultVideoInputFormat() : formatName;
	if (fmt != QStringLiteral( "dshow" ) || device.originalId.isEmpty())
		return QString();
	// 兜底输入与主输入相同时直接返回空
	if (device.originalId.compare( device.name, Qt::CaseInsensitive ) == 0)
		return QString();
	return QStringLiteral( "video=%1" ).arg( device.originalId );
}

// ==========
StreamInfo FfmpegDecoder::getStreamInfo() const {
	return m_streamInfo;
}

DecodeError FfmpegDecoder::getLastError() const {
	return m_lastError;
}

bool FfmpegDecoder::isOpened() const {
	return m_running.load( std::memory_order_relaxed );
}
void FfmpegDecoder::close() {
	// 请求停止
	requestStop();
	// 释放资源
	this->reset();
	m_running.store( false, std::memory_order_relaxed );  // 不再运行
}
// 设置配置(注意需要在未解码时使用，正在解码时触发错误，不会静默更新)
void FfmpegDecoder::setConfig( const DecodeConfig& config ) {
	if (isOpened()) {
		m_lastError = DecodeError::InvalidConfigError( QStringLiteral( "正在解码，无法修改配置" ) );
		return;
	}
	m_config = config;
}
// 设置硬件解码设备,自持引用
void FfmpegDecoder::setHwDevice( AVBufferRef* hwDevice ) {
	if (isOpened()) {
		m_lastError = DecodeError::InvalidConfigError( QStringLiteral( "正在解码，无法修改硬件解码设备" ) );
		return;
	}
	m_hwDevice = hwDevice ? av_buffer_ref(hwDevice) : nullptr;
}
// 请求停止（线程安全）
void  FfmpegDecoder::requestStop() {
	{
		std::lock_guard<std::mutex> lock( m_pauseMutex );
		m_interruptContex.stop.store( true, std::memory_order_relaxed );
	}
	m_pauseCV.notify_all(); // 唤醒阻塞进程，防止因为中断而不能够进行停止
}
// 是否请求停止
bool FfmpegDecoder::isStopRequested() const {
	return m_interruptContex.stop.load( std::memory_order_relaxed );
}
// 设置暂停/恢复（线程安全）
void FfmpegDecoder::setPaused( bool pause ) {
	{
		std::lock_guard<std::mutex> lock( m_pauseMutex );
		m_interruptContex.pause.store( pause, std::memory_order_relaxed );
	}
	m_pauseCV.notify_all(); // 唤醒阻塞进程，防止因为中断而不能够进行停止
}
// 是否暂停
bool FfmpegDecoder::isPaused() const {
	return m_interruptContex.pause.load( std::memory_order_relaxed );
}

DecodeStatistics FfmpegDecoder::getStatistics() const {
	return m_decodeStatistics;
}
// 清理所有资源
void FfmpegDecoder::releaseResources() {
	m_frame.reset();
	m_packet.reset();
	// hw_frames_ctx挂在codecContext上，随其释放自动unref
	m_codecContext.reset();
	m_formatContext.reset();
	av_buffer_unref( &m_hwDevice );	// 自持引用，释放引用
	m_videoStreamIndex = -1;
	has_pendingPacket = false;
	flushEof = false;
}
// 清除所有资源以及重置配置和统计信息等
void  FfmpegDecoder::reset() {
	releaseResources();
	m_videoStreamIndex = -1;
	m_config = DecodeConfig();
	m_streamInfo = StreamInfo();
	m_lastError = DecodeError::NoError();
	m_frameCallback = nullptr;
	m_decodeIndex = 0;
	flushEof = false;
	has_pendingPacket = false;
	m_decodeStatistics.reset();
}

// 打开输入(获取formatContext,设置回调和选项，查找流信息)
bool FfmpegDecoder::openInput() {
	// 如果已经打开，直接返回
	if (m_formatContext && m_codecContext) {
		return true;
	}
	// 重置错误
	m_lastError = DecodeError::NoError();

	// 创建AVFormatContext并设置中断回调、选项
	AVFormatContext* rawFormatContext = avformat_alloc_context();
	if (!rawFormatContext) {
		m_lastError = DecodeError::OpenFailedError( QStringLiteral( "无法分配AVFormatContext" ) );
		return false;
	}
	rawFormatContext->interrupt_callback.opaque = &m_interruptContex;
	rawFormatContext->interrupt_callback.callback = &FfmpegDecode::interrupt_cb;
	// 设置选项
	AVDictionary* options = nullptr;
	av_dict_set_int( &options, "timeout", static_cast<int64_t>(m_config.connectTimeoutMs) * 1000, 0 );	 // 设置超时时间(us)
	// 设置其他选项，例如 rtsp_transport
	for (const auto& [key, value] : m_config.options) {
		if (int optRet = av_dict_set( &options, key.toUtf8().constData(), value.toUtf8().constData(), 0 ); optRet < 0) {
			m_lastError = DecodeError::OpenFailedError( QStringLiteral( "设置选项失败，错误码: %1 选项：%2 值：%3" ).arg( FfmpegDecode::ffmepegError( optRet ) ).arg( key ).arg( value ) );
			av_dict_free( &options );
			return false;
		}
	}
	m_interruptContex.timeoutMs = m_config.connectTimeoutMs; // 连接阶段使用连接超时
	m_interruptContex.restart(); // 开启连接时重启计时

	const AVInputFormat* inputFormat = nullptr;
	if (m_config.inputFormat && !m_config.inputFormat->isEmpty()) {
		inputFormat = av_find_input_format( m_config.inputFormat->toUtf8().constData() );
		if (!inputFormat) {
			m_lastError = DecodeError::OpenFailedError( QStringLiteral( "无法找到输入格式: %1" ).arg( *m_config.inputFormat ) );
			av_dict_free( &options );
			return false;
		}
	}

	// 设备源使用 deviceInput
	// 图片、视频文件等使用 URL 
	const bool isDevice = m_config.isDevice();	 // 检查是否为设备
	// 生成输入串（根据是否为设备）
	const auto toInputBytes = [ isDevice ]( const DecodeConfig& config ) -> QByteArray {
		if (isDevice)
			return config.deviceInput->toUtf8();
		return config.url.isLocalFile()
			? QFile::encodeName( config.url.toLocalFile() )
			: config.url.toString( QUrl::FullyEncoded ).toUtf8();
		};
	const QString inputLabel = isDevice ? m_config.deviceInput.value() : m_config.url.toString();

	// 打开输入；设备源在主输入失败时用兜底输入（dshow 的 PNP 路径）再试一次
	QByteArray inputBytes = toInputBytes( m_config );
	int openRet = avformat_open_input( &rawFormatContext, inputBytes.constData(), inputFormat, &options );
	// 如果打开失败并且是设备、存在兜底输入，重新尝试
	if (openRet < 0 && isDevice && m_config.deviceFallbackInput && !m_config.deviceFallbackInput->isEmpty()) {
		// avformat_open_input 失败时会自行释放 context 并置空，重试前需要重新分配
		rawFormatContext = avformat_alloc_context();
		if (rawFormatContext) {
			rawFormatContext->interrupt_callback.opaque = &m_interruptContex;
			rawFormatContext->interrupt_callback.callback = &FfmpegDecode::interrupt_cb;
			const QByteArray fallbackBytes = m_config.deviceFallbackInput->toUtf8();
			openRet = avformat_open_input( &rawFormatContext, fallbackBytes.constData(), inputFormat, &options );
			if (openRet >= 0)
				Logger::instance()->info( QStringLiteral( "[FfmpegDecoder]: 友好名打开失败，已回退到设备标识打开: %1" ).arg( *m_config.deviceFallbackInput ) );
		}
		else {
			openRet = AVERROR( ENOMEM );
		}
	}
	av_dict_free( &options );
	if (openRet < 0) {
		// 注意：avformat_open_input 失败时会自行释放 context，并把指针置为 nullptr
		if (isStopRequested()) // 其他线程请求停止，直接返回
			return false;
		if (m_interruptContex.isTimeout()) {
			m_lastError = DecodeError::ReadTimeoutError( QStringLiteral( "打开输入超时: %1" ).arg( inputLabel ) );
		}
		else {
			m_lastError = DecodeError::OpenFailedError( QStringLiteral( "无法打开输入: %1 输入: %2" ).arg( FfmpegDecode::ffmepegError( openRet ), inputLabel ) );
		}
		return false;
	}
	m_formatContext.reset( rawFormatContext );

	// 设置读取流信息阶段的超时
	m_interruptContex.timeoutMs = m_config.readTimeoutMs;
	m_interruptContex.restart();

	// 查找流信息
	if (int infoRet = avformat_find_stream_info( m_formatContext.get(), nullptr );infoRet < 0) {
		if (isStopRequested())
			return false;
		if (m_interruptContex.isTimeout()) {
			m_lastError = DecodeError::ReadTimeoutError( QStringLiteral( "查找流信息超时: %1" ).arg( inputLabel ) );
		}
		else {
			m_lastError = DecodeError::OpenFailedError( QStringLiteral( "无法查找流信息: %1" ).arg( FfmpegDecode::ffmepegError( infoRet ) ) );
		}
		return false;
	}
	return true;
}

// 根据视频流初始化解码器和 packet/frame
bool FfmpegDecoder::initDecoder() {
	m_streamInfo.reset();
	// 校验上下文和视频流索引
	if (!m_formatContext) {
		m_lastError = DecodeError::OpenFailedError( QStringLiteral( "格式上下文为空" ) );
		return false;
	}
	if (m_config.videoStreamIndex >= 0) {
		if (m_config.videoStreamIndex >= static_cast<int>(m_formatContext->nb_streams)
			|| m_formatContext->streams[m_config.videoStreamIndex]->codecpar->codec_type != AVMEDIA_TYPE_VIDEO) {
			m_lastError = DecodeError::StreamNotFoundError( QStringLiteral( "指定的视频流索引无效: %1" ).arg( m_config.videoStreamIndex ) );
			return false;
		}
		m_videoStreamIndex = m_config.videoStreamIndex;
	}
	else {
		m_videoStreamIndex = av_find_best_stream( m_formatContext.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0 );
	}
	if (m_videoStreamIndex < 0) {
		m_lastError = DecodeError::StreamNotFoundError( QStringLiteral( "无法找到视频流: %1" ).arg( FfmpegDecode::ffmepegError( m_videoStreamIndex ) ) );
		return false;
	}
	// 查找解码器
	const AVCodec* codec = avcodec_find_decoder( m_formatContext->streams[m_videoStreamIndex]->codecpar->codec_id );
	if (!codec) {
		m_lastError = DecodeError::CodecNotFoundError( QStringLiteral( "无法找到解码器" ) );
		return false;
	}
	// 根据视频流创建解码器上下文
	m_codecContext.reset( avcodec_alloc_context3( codec ) );
	if (!m_codecContext) {
		m_lastError = DecodeError::DecodedrInitFailedError( QStringLiteral( "无法创建解码器上下文" ) );
		return false;
	}
	// 获取视频流参数到解码器上下文（hw_device_ctx需要在open2之前挂好）
	AVStream* stream = m_formatContext->streams[m_videoStreamIndex];
	if (int ret = avcodec_parameters_to_context( m_codecContext.get(), stream->codecpar ); ret < 0) {
		m_lastError = DecodeError::OpenFailedError( QStringLiteral( "无法初始化解码器上下文: %1" ).arg( FfmpegDecode::ffmepegError( ret ) ) );
		return false;
	}
	// GPU硬解码：注入hw_device_ctx并预建显存帧缓冲池
	if (m_config.useGpuDecode) {
		if (!m_hwDevice) {
			m_lastError = DecodeError::DecodedrInitFailedError( QStringLiteral( "启用GPU解码但未设置硬件设备，请先调用setHwDevice" ) );
			return false;
		}
		m_codecContext->hw_device_ctx = av_buffer_ref( m_hwDevice );
		if (!m_codecContext->hw_device_ctx) {
			m_lastError = DecodeError::DecodedrInitFailedError( QStringLiteral( "无法引用硬件设备上下文" ) );
			return false;
		}
		// 初始化硬件帧上下文（缓冲）
		if (!initHwFramesContext()) {
			return false;
		}
	}
	// 开启解码器
	if (int ret = avcodec_open2( m_codecContext.get(), codec, nullptr ); ret < 0) {
		m_lastError = DecodeError::OpenFailedError( QStringLiteral( "无法打开解码器: %1" ).arg( FfmpegDecode::ffmepegError( ret ) ) );
		return false;
	}
	// 验证硬解协商结果
	// FFmpeg会静默回退到软件解码
	// 显示上报fallback
	if (m_config.useGpuDecode && !m_codecContext->hw_frames_ctx) {
		m_lastError = DecodeError::DecodedrInitFailedError( QStringLiteral( "硬解协商失败: 编解码器 %1 不支持NVDEC或回退到了软件解码" ).arg( codec->name ) );
		return false;
	}
	// 分配AVPacket和AVFrame
	m_packet.reset( av_packet_alloc() );
	if (!m_packet) {
		m_lastError = DecodeError::OpenFailedError( QStringLiteral( "无法创建AVPacket" ) );
		return false;
	}
	m_frame.reset( av_frame_alloc() );
	if (!m_frame) {
		m_lastError = DecodeError::OpenFailedError( QStringLiteral( "无法创建AVFrame" ) );
		return false;
	}
	// 读取并记录流信息
	recordStreamInfo( stream, codec );
	return true;
}

// 从借用的hwdevice派生显存帧缓冲池(控制显存占用)并挂到解码器
// 句柄持帧时池子必须还有空闲buffer，否则解码器拿不到新buffer而阻塞/丢帧
bool FfmpegDecoder::initHwFramesContext() {
	// 从CudaDevice的hwdevice分配，保证和推理/预处理用同一个CUDA设备
	AVBufferRef* derivedFrames = av_hwframe_ctx_alloc( m_hwDevice );
	if (!derivedFrames) {
		m_lastError = DecodeError::DecodedrInitFailedError( QStringLiteral( "无法从硬件设备分配帧缓冲池" ) );
		return false;
	}
	auto* framesCtx = reinterpret_cast<AVHWFramesContext*>(derivedFrames->data);
	framesCtx->format = AV_PIX_FMT_CUDA; // 硬件像素格式（NVDEC输出标识）
	framesCtx->sw_format = AV_PIX_FMT_NV12; 
	framesCtx->width = m_codecContext->width;
	framesCtx->height = m_codecContext->height;
	framesCtx->initial_pool_size = 10;
	if (int ret = av_hwframe_ctx_init( derivedFrames ); ret < 0) {
		m_lastError = DecodeError::DecodedrInitFailedError( QStringLiteral( "初始化帧缓冲池失败: %1" ).arg( FfmpegDecode::ffmepegError( ret ) ) );
		av_buffer_unref( &derivedFrames );
		return false;
	}
	m_codecContext->hw_frames_ctx = derivedFrames;
	return true;
}

// 记录当前的流信息到 m_streamInfo
void FfmpegDecoder::recordStreamInfo(  AVStream* stream, const AVCodec* codec ) {
	m_streamInfo.reset();
	if (!stream || !codec || !m_formatContext)
		return;
	const auto text = [ ]( const char* value, const QString& fallback ) {
		return value ? QString::fromUtf8( value ) : fallback;
		};
	m_streamInfo.containerFormat = text( m_formatContext->iformat ? m_formatContext->iformat->name : nullptr, QStringLiteral( "unknown" ) );
	m_streamInfo.codecName = text( codec->name, QStringLiteral( "unknown" ) );
	m_streamInfo.formatName = text( m_formatContext->iformat ? m_formatContext->iformat->long_name : nullptr, m_streamInfo.containerFormat );
	AVPixelFormat pixelFormat = m_codecContext ? m_codecContext->pix_fmt : AV_PIX_FMT_NONE;
	if (pixelFormat == AV_PIX_FMT_NONE)
		pixelFormat = static_cast<AVPixelFormat>(stream->codecpar->format);
	m_streamInfo.pixelFormat = text( av_get_pix_fmt_name( pixelFormat ), QStringLiteral( "unknown" ) );
	m_streamInfo.colorSpace = text( av_color_space_name( stream->codecpar->color_space ), QStringLiteral( "unspecified" ) );
	m_streamInfo.colorRange = text( av_color_range_name( stream->codecpar->color_range ), QStringLiteral( "unspecified" ) );
	m_streamInfo.timeBase = stream->time_base;
	AVRational frameRate = av_guess_frame_rate( m_formatContext.get(), stream, nullptr );
	if (frameRate.num <= 0 || frameRate.den <= 0)
		frameRate = stream->r_frame_rate;
	if (frameRate.num <= 0 || frameRate.den <= 0)
		frameRate = AVRational{ 0, 1 };
	m_streamInfo.frameRate = frameRate;
	m_streamInfo.width = (m_codecContext && m_codecContext->width > 0) ? m_codecContext->width : stream->codecpar->width;
	m_streamInfo.height = (m_codecContext && m_codecContext->height > 0) ? m_codecContext->height : stream->codecpar->height;
	// 总帧数：流头记录的 nb_frames 最可靠；缺失时用时长×帧率估算并标记为估算值。
	// 直播流通常两者都没有，保持 0 表示未知（FrameSource::totalFrames 的约定）。
	m_streamInfo.totalFrames = 0;
	m_streamInfo.totalFramesEstimated = false;
	if (stream->nb_frames > 0) {
		m_streamInfo.totalFrames = stream->nb_frames;
	}
	else if (frameRate.num > 0 && frameRate.den > 0) {
		int64_t duration = stream->duration;
		AVRational durationTimeBase = stream->time_base;
		if (duration <= 0) {
			// 流级时长缺失时退回容器时长（单位为 AV_TIME_BASE）
			duration = m_formatContext->duration;
			durationTimeBase = AV_TIME_BASE_Q;
		}
		if (duration > 0) {
			const int64_t estimated = av_rescale_q( duration, durationTimeBase, av_inv_q( frameRate ) );
			if (estimated > 0) {
				m_streamInfo.totalFrames = estimated;
				m_streamInfo.totalFramesEstimated = true;
			}
		}
	}
	m_streamInfo.videoStreamIndex = stream->index;
}
// 设置回调函数
void FfmpegDecoder::setFrameCallback( const frameCallback& callback ) {
		m_frameCallback = callback;
}

// 读取packet并解码，阻塞直到停止、EOF或出错
bool FfmpegDecoder::readAndDecodeLoop() {
	bool failed = false;  // 失败
	bool cleanEof = false;		   // 是否正常结束
	m_interruptContex.timeoutMs = m_config.readTimeoutMs; // 读取阶段使用读取超时
	// 循环读取packet并解码
	while (!isStopRequested()) {
		// 检查暂停
		if (isPaused()) {
			// 如果暂停，阻塞当前的线程，直到恢复或停止
			std::unique_lock<std::mutex> lock( m_pauseMutex );
			m_pauseCV.wait( lock, [ this ]() { return !isPaused() || isStopRequested(); } );
			if (!isPaused())
				continue; // 恢复后继续循环
			if (isStopRequested())// 停止直接退出
				break;
		}
		// 每次读取前重置超时计时
		m_interruptContex.restart();
		// 读取packet
		int readRet = av_read_frame( m_formatContext.get(), m_packet.get() );
		quint64 readTimeUs = av_gettime(); // 记录读取压缩包的时间(us)

		// 如果出现错误
		if (readRet < 0) {
			// 检查顺序：1.EOF 2. 停止请求 3. 中断请求 4. 超时 5. 其他错误
			if (readRet == AVERROR_EOF) {
				cleanEof = true;
				break;
			}
			else if (isStopRequested()) {
				break;
			}
			else if (isPaused()) {
				continue;
			}
			else if (readRet == AVERROR_EXIT) {
				m_lastError = DecodeError::ReadTimeoutError( QStringLiteral( "读取数据包超时" ) );
				failed = true;
			}
			else {
				m_lastError = DecodeError::ReadFailedError( QStringLiteral( "读取帧失败: %1" ).arg( FfmpegDecode::ffmepegError( readRet ) ) );
				failed = true;
			}
			break;
		}

		// 记录读取统计等信息
		++m_decodeStatistics.receivedPacketCount;
		if (m_packet->stream_index == m_videoStreamIndex) {
			++m_decodeStatistics.videoPacketCount;
			m_decodeStatistics.lastReadTime = readTimeUs;
		}

		// 非视频packet及时unref
		if (m_packet->stream_index != m_videoStreamIndex) {
			av_packet_unref( m_packet.get() );
			continue;
		}

		// 发送给解码器进行解码
		int sendRet = 0;
		while (true) {
			sendRet = avcodec_send_packet( m_codecContext.get(), m_packet.get() );
			if (sendRet != AVERROR( EAGAIN ))
				break;
			// 解码器输入队列满，先收帧再重发同一个packet
			if (!receiveFrames()) {
				failed = true;
				break;
			}
			if (isStopRequested())
				break;
		}
		av_packet_unref( m_packet.get() );	// 释放计数
		if (failed || isStopRequested())
			break;
		// 出现解码错误，返回错误
		if (sendRet < 0 && sendRet != AVERROR_EOF) {
			++m_decodeStatistics.decodeErrorCount;
			m_lastError = DecodeError::ReadFailedError( QStringLiteral( "发送数据包失败: %1" ).arg( FfmpegDecode::ffmepegError( sendRet ) ) );
			failed = true;
			break;
		}
		// 发送后在接收解码器的输出帧
		if (!receiveFrames()) {
			failed = true;
			break;
		}
	}
	// 正常EOF时需要flush解码器内部缓存的帧
	if (cleanEof && !isStopRequested()) {
		avcodec_send_packet( m_codecContext.get(), nullptr );
		if (!receiveFrames())
			failed = true;
	}
	return !failed;
}
// 循环接收帧
bool FfmpegDecoder::receiveFrames() {
	while (true) {
		// 如果停止，取消
		if (isStopRequested())
			return true;
		// 接收帧
		const int ret = avcodec_receive_frame( m_codecContext.get(), m_frame.get() );
		quint64 decodedTimeUs = av_gettime(); // 记录解码帧的时间(us)
		// 如果等待后续的packet或者冲刷完，返回true
		if (ret == AVERROR( EAGAIN ) || ret == AVERROR_EOF) {
			return true;
		}
		// 解码失败
		if (ret < 0) {
			++m_decodeStatistics.decodeErrorCount;
			m_lastError = DecodeError::ReadFailedError( QStringLiteral( "解码帧失败: %1" ).arg( FfmpegDecode::ffmepegError( ret ) ) );
			av_frame_unref( m_frame.get() );
			return false;
		}
		// 记录解码统计
		++m_decodeStatistics.decodedFrameCount;
		m_decodeStatistics.lastDecodeTime = decodedTimeUs;
		const int64_t pts = m_frame->best_effort_timestamp;
		m_decodeStatistics.lastPts = (pts == AV_NOPTS_VALUE) ? -1 : pts;
		if (m_frameCallback)
			m_frameCallback( m_frame.get(), m_decodeStatistics.lastReadTime, decodedTimeUs );
		av_frame_unref( m_frame.get() );
	}
}

// 打开
bool FfmpegDecoder::open() {
	// 如果已经打开，直接返回
	bool expected = false;
	if (!m_running.compare_exchange_strong( expected, true, std::memory_order_acq_rel )) {
		return true;
	}
	// 复位所有标志位
	m_interruptContex.resetStatus();
	m_decodeStatistics.reset();
	m_lastError = DecodeError::NoError();
	m_decodeIndex = 0;
	has_pendingPacket = false;
	flushEof = false;

	// 校验输入有效：设备源看 deviceInput，其余看 url
	if (!m_config.isValid()) {
		const QString what = m_config.isDevice()
			? QStringLiteral( "设备输入无效: %1" ).arg( *m_config.deviceInput )
			: QStringLiteral( "URL无效: %1" ).arg( m_config.url.toString() );
		m_lastError = DecodeError::OpenFailedError( what );
		m_running.store(false,std::memory_order_relaxed);
		return false;
	}
	// 初始化
	static std::once_flag networkInitOnce;
	std::call_once( networkInitOnce, [ ] {
		avformat_network_init();
		avdevice_register_all();
		} );

	const bool OK = openInput() && initDecoder();
	if (!OK) {
		m_streamInfo.reset();
		releaseResources();
		m_running.store( false, std::memory_order_relaxed );
		return false;
	}
	return true;
}

// 开启读取循环
bool FfmpegDecoder::startDecodingLoop() {
	// 如果没有打开，尝试打开
	if (!isOpened()) {
		if (!open()) {
			return false;
		}
	}
	m_interruptContex.timeoutMs = m_config.readTimeoutMs; // 读取阶段使用读取超时
	const bool OK = readAndDecodeLoop();
	return OK;
}

// 读取下一帧，返回读取的状态
// 调用方需要判断是否有帧可用，可用时可使用getCurrentFrame获取原始指针进行操作（仅保证在下一次的readNextFrame()之前有效）
ReadStatus FfmpegDecoder::readNextFrame() {
	m_interruptContex.timeoutMs = m_config.readTimeoutMs; // 使用读取超时
	// 如果已经停止，直接返回
	if (isStopRequested()) {
		return ReadStatus::Stopped;
	}
	if (isPaused()) {
		return ReadStatus::Paused;
	}
	// 如果没有打开，尝试打开
	if (!isOpened()) {
		if (!open()) {
			return ReadStatus::Error;
		}
	}
	// 下一帧
	while (true)
	{
		// 先尝试获取已经解码出来的帧
		int ret = avcodec_receive_frame( m_codecContext.get(), m_frame.get() );
		if (ret == 0) {
			++m_decodeStatistics.decodedFrameCount;
			++m_decodeIndex;
			m_decodeStatistics.lastDecodeTime = av_gettime();
			const int64_t pts = m_frame->best_effort_timestamp;
			m_decodeStatistics.lastPts = (pts == AV_NOPTS_VALUE) ? -1 : pts;
			return ReadStatus::FrameReady;
		}
		if (ret < 0) {
			if (ret == AVERROR_EOF) {
				return ReadStatus::EndOfFile;
			}
			if (ret != AVERROR( EAGAIN )) {
				++m_decodeStatistics.decodeErrorCount;
				m_lastError = DecodeError::ReadFailedError( QStringLiteral( "解码帧失败: %1" ).arg( FfmpegDecode::ffmepegError( ret ) ) );
				return ReadStatus::Error;
			}
		}
		// 如果没有解码出来的帧，则读取新的packet并解码
		if (m_packet && has_pendingPacket) {	  // 如果当前还有packet没有发送完，发送
			ret = avcodec_send_packet( m_codecContext.get(), m_packet.get() );
			if (ret == AVERROR( EAGAIN ))continue;
			av_packet_unref( m_packet.get() );
			has_pendingPacket = false;	 // 发送后进行复位
			if (ret < 0 && ret != AVERROR_EOF) {
				++m_decodeStatistics.decodeErrorCount;
				m_lastError = DecodeError::DecodeFailedError( QStringLiteral( "发送数据包失败: %1" ).arg( FfmpegDecode::ffmepegError( ret ) ) );
				return ReadStatus::Error;
			}
			continue;
		}
		// 如果已经flush过EOF，则直接返回EndOfFile
		if (flushEof) {
			return ReadStatus::EndOfFile;
		}
		m_interruptContex.restart(); // 读取前，重置超时计时
		// 读取新的packet
		ret = av_read_frame( m_formatContext.get(), m_packet.get() );
		if (ret == AVERROR_EOF) {
			// 正常EOF，flush解码器
			avcodec_send_packet( m_codecContext.get(), nullptr );
			flushEof = true;
			continue;
		}
		// 出现问题：
		if (ret < 0) {
			// 如果停止、暂停、其他错误
			if (isStopRequested()) {
				return ReadStatus::Stopped;
			}
			if (isPaused()) {
				return ReadStatus::Paused;
			}
			if (ret == AVERROR_EXIT) {
				m_lastError = DecodeError::ReadTimeoutError( QStringLiteral( "读取数据包超时" ) );
			}
			else {
				m_lastError = DecodeError::ReadFailedError( QStringLiteral( "读取数据包失败: %1" ).arg( FfmpegDecode::ffmepegError( ret ) ) );
			}
			return ReadStatus::Error;
		}
		++m_decodeStatistics.receivedPacketCount;
		// 非视频packet及时unref
		if (m_packet->stream_index != m_videoStreamIndex) {
			av_packet_unref( m_packet.get() );
			continue;
		}
		++m_decodeStatistics.videoPacketCount;
		// 获取读取时间戳
		m_decodeStatistics.lastReadTime = av_gettime();
		has_pendingPacket = true;  // 得到了packet,标记
	}

}


// 借用指针,在下一次readNextFrame()之前有效(后续读取会unref前面的AVFrame，m_frame后面也会被覆盖)
const AVFrame* FfmpegDecoder::getCurrentFrame()const {
	return m_frame.get();
}

