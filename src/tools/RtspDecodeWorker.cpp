#include "RtspDecodeWorker.h"

#include <cmath>

// ===== RtspDecodeWorker =====
RtspDecodeWorker::RtspDecodeWorker( QObject* parent )
	: QObject( parent )
{
	qRegisterMetaType<RtspStatus>( "RtspStatus" );
	qRegisterMetaType<RtspOverallError>( "RtspOverallError" );
	qRegisterMetaType<LastestFrameStore>( "LastestFrameStore" );
}

RtspDecodeWorker::RtspDecodeWorker( const RtspConfig& config, QObject* parent )
	: QObject( parent )
	, m_config( config )
	, m_rtspUrl( config.url )
{
	qRegisterMetaType<RtspStatus>( "RtspStatus" );
	qRegisterMetaType<RtspOverallError>( "RtspOverallError" );
	qRegisterMetaType<LastestFrameStore>( "LastestFrameStore" );
}

RtspDecodeWorker::~RtspDecodeWorker() {
	this->reset();
}

// 统一上报错误：发送 errorOccurred + statusChanged(Error)，
//  controller 连接两处信号同步错误和状态
void RtspDecodeWorker::reportError( const RtspOverallError& error ) {
	emit errorOccurred( error );
	emit statusChanged( RtspStatus::Error );
}

// 清理所有资源
// 顺序：先释放只属于解码过程的 frame/packet，再释放解码器，最后关闭输入。
void RtspDecodeWorker::releaseResources() {
	if (m_swsContext) {
		sws_freeContext( m_swsContext );
		m_swsContext = nullptr;
	}
	m_frame.reset();
	m_packet.reset();
	m_codecContext.reset();
	m_formatContext.reset();
	m_videoStreamIndex = -1;
}

// 清理所有保存的配置数据并回收资源
void RtspDecodeWorker::reset() {
	m_config = RtspConfig{};
	m_rtspUrl = QUrl{};
	m_dataContext = RtspDataContext{};
	m_decodedFrameIndex = 0;
	releaseResources();
}
// 设置RTSP URL
// 无效由解码时进行发出错误
void RtspDecodeWorker::setRtspUrl( const QString& url ) {
	m_rtspUrl = QUrl( url );
	m_config.url = m_rtspUrl;
}
// 获取RTSP URL
QUrl RtspDecodeWorker::getRtspUrl() const {
	return m_rtspUrl;
}
// 设置RTSP配置
// 无效由解码时进行发出错误
void RtspDecodeWorker::setRtspConfig( const RtspConfig& config ) {
	m_config = config;
	m_rtspUrl = config.url;
}
// 设置数据上下文
void RtspDecodeWorker::setRtspDataContext( const RtspDataContext& context ) {
	m_dataContext = context;
}
// 获取数据上下文
RtspDataContext RtspDecodeWorker::getRtspDataContext() const {
	return m_dataContext;
}

// 检查是否正在解码
bool RtspDecodeWorker::isRunning() const {
	return m_running.load( std::memory_order_relaxed );
}

// 检查是否其他线程请求了停止
bool RtspDecodeWorker::isStopRequested() const {
	return m_stopRequested.load( std::memory_order_relaxed )
		|| m_interruptContext.abort.load( std::memory_order_relaxed );
}

// 线程安全：只写原子变量，不碰 Qt 对象和 FFmpeg 对象。
// 必须在 GUI/控制线程里直接调用（或 Qt::DirectConnection），
// 否则 queued 调用会一直排在正在阻塞的解码线程的事件队列里。
void RtspDecodeWorker::requestStop() {
	m_stopRequested.store( true, std::memory_order_relaxed );
	m_interruptContext.abort.store( true, std::memory_order_relaxed );
}

// 开始解码
void RtspDecodeWorker::startDecoding() {
	// 校验是否正在解码中，如果是则直接返回错误
	// 防止重复 start 在同一对象上并发执行
	bool expected = false;
	if (!m_running.compare_exchange_strong( expected, true, std::memory_order_acq_rel )) {
		reportError( RtspOverallError( QStringLiteral( "解码已在运行" ), RtspOverallError::InvalidConfig ) );
		return;
	}

	// 每次启动都重置停止标志；否则上一次 stop 的 abort 会让这次立即退出
	m_stopRequested.store( false, std::memory_order_relaxed );
	m_interruptContext.abort.store( false, std::memory_order_relaxed );
	m_decodedFrameIndex = 0;

	// 校验URL
	if (m_rtspUrl.isEmpty() || !m_rtspUrl.isValid() || m_rtspUrl.scheme().isEmpty()) {
		reportError( RtspOverallError::InvalidUrlError( QStringLiteral( "无效的URL: " ) + m_rtspUrl.toString() ) );
		m_running.store( false, std::memory_order_relaxed );
		return;
	}
	// 校验配置
	if (!m_config.isValid()) {
		reportError( RtspOverallError::InvalidConfigError( QStringLiteral( "无效配置" ) ) );
		m_running.store( false, std::memory_order_relaxed );
		return;
	}
	// 初始化网络模块（只初始化一次）
	static std::once_flag networkInitOnce;
	std::call_once( networkInitOnce, [ ] {
		avformat_network_init(); // 初始化网络模块
		} );

	emit statusChanged( RtspStatus::Opening );
	// 打开输入并初始化解码器，进入读取和解码循环
	const bool ok = openInput() && initDecoder() && readAndDecodeLoop();
	Q_UNUSED( ok );

	// 统一释放所有资源
	releaseResources();
	m_running.store( false, std::memory_order_relaxed );
	// 解码循环退出（正常 EOF / 被请求停止 / 失败已另行上报 Error）后，回到 Stopped
	emit statusChanged( RtspStatus::Stopped );
}

// 打开输入并初始化
bool RtspDecodeWorker::openInput() {
	// 如果 URL 里没有用户名/密码，而配置里提供了，则补进去
	QUrl openUrl = m_rtspUrl;
	if (m_config.username && !m_config.username->isEmpty() && openUrl.userName().isEmpty())
		openUrl.setUserName( *m_config.username );
	if (m_config.password && !m_config.password->isEmpty() && openUrl.password().isEmpty())
		openUrl.setPassword( *m_config.password );

	// 本地文件用本地路径
	// RTSP 等网络 URL 用 FullyEncoded 的标准形式。
	QByteArray urlBytes;
	if (openUrl.isLocalFile())
		urlBytes = openUrl.toLocalFile().toUtf8();
	else
		urlBytes = openUrl.toString( QUrl::FullyEncoded ).toUtf8();
	if (urlBytes.isEmpty()) {
		reportError( RtspOverallError::InvalidUrlError( QStringLiteral( "无效的URL" ) ) );
		return false;
	}

	// 1.创建AVFormatContext并设置中断回调函数
	AVFormatContext* rawFormatContext = avformat_alloc_context();
	if (!rawFormatContext) {
		reportError( RtspOverallError( QStringLiteral( "无法创建AVFormatContext" ), RtspOverallError::Other ) );
		return false;
	}
	rawFormatContext->interrupt_callback.opaque = &m_interruptContext;
	rawFormatContext->interrupt_callback.callback = &Decode::interrupt_cb;

	// 连接阶段用连接超时；每次阻塞操作前重新计时，
	m_interruptContext.timeoutMs = m_config.connectTimeoutMs;
	m_interruptContext.restart();

	// 设置tcp并设置超时时间（us）
	// RTSP 默认可能走 UDP，UDP 丢包不稳定且键盘中断响应差
	// 当前先固定使用TCP
	AVDictionary* options = nullptr;
	av_dict_set( &options, "rtsp_transport", "tcp", 0 );
	av_dict_set_int( &options, "timeout", static_cast<int64_t>(m_config.connectTimeoutMs) * 1000, 0 );

	// 2.打开RTSP流
	const int openRet = avformat_open_input( &rawFormatContext, urlBytes.constData(), nullptr, &options );
	av_dict_free( &options );

	if (openRet < 0) {
		// 注意：avformat_open_input 失败时会自行释放 context，并把指针置为 nullptr
		// 这里不能再调用 avformat_close_input，否则重复释放。
		if (isStopRequested()) {
			return false;
		}
		if (m_interruptContext.isTimeout()) {
			reportError( RtspOverallError::ReadTimeoutError(
				QStringLiteral( "连接RTSP超时: " ) + openUrl.toString() ) );
		}
		else {
			reportError( RtspOverallError::ConnectionFailedError(
				QStringLiteral( "无法打开RTSP流: " ) + Decode::ffmpegError( openRet ) ) );
		}
		return false;
	}
	m_formatContext.reset( rawFormatContext );

	// 读取流信息阶段用读取超时
	m_interruptContext.timeoutMs = m_config.readTimeoutMs;
	m_interruptContext.restart();

	// 3.查找视频流
	const int infoRet = avformat_find_stream_info( m_formatContext.get(), nullptr );
	if (infoRet < 0) {
		if (isStopRequested())
			return false;
		if (m_interruptContext.isTimeout()) {
			reportError( RtspOverallError::ReadTimeoutError(
				QStringLiteral( "读取RTSP流信息超时: " ) + Decode::ffmpegError( infoRet ) ) );
		}
		else {
			reportError( RtspOverallError::ConnectionFailedError(
				QStringLiteral( "无法查找流信息: " ) + Decode::ffmpegError( infoRet ) ) );
		}
		return false;
	}

	// 输入已打开且流信息可用，上报 Opened
	emit statusChanged( RtspStatus::Opened );
	return true;
}

// 初始化解码器
bool RtspDecodeWorker::initDecoder() {
	m_dataContext.reset();

	m_videoStreamIndex = av_find_best_stream( m_formatContext.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0 );
	if (m_videoStreamIndex < 0) {
		reportError( RtspOverallError::StreamNotFoundError(
			QStringLiteral( "无法找到视频流: " ) + Decode::ffmpegError( m_videoStreamIndex ) ) );
		return false;
	}

	// 4.查找解码器
	const AVCodec* codec = avcodec_find_decoder( m_formatContext->streams[m_videoStreamIndex]->codecpar->codec_id );
	if (!codec) {
		reportError( RtspOverallError( QStringLiteral( "无法找到解码器" ), RtspOverallError::StreamNotFound ) );
		return false;
	}

	// 5.根据视频流创建解码器上下文
	// 分配后立刻交给 unique_ptr，避免后续任何失败分支泄漏
	m_codecContext.reset( avcodec_alloc_context3( codec ) );
	if (!m_codecContext) {
		reportError( RtspOverallError( QStringLiteral( "无法创建解码器上下文" ), RtspOverallError::Other ) );
		return false;
	}

	// 6.获取视频流
	AVStream* stream = m_formatContext->streams[m_videoStreamIndex];
	if (const int ret = avcodec_parameters_to_context( m_codecContext.get(), stream->codecpar ); ret < 0) {
		reportError( RtspOverallError( QStringLiteral( "无法初始化解码器上下文: " ) + Decode::ffmpegError( ret ), RtspOverallError::Other ) );
		return false;
	}

	// 开启解码器
	if (const int ret = avcodec_open2( m_codecContext.get(), codec, nullptr ); ret < 0) {
		reportError( RtspOverallError( QStringLiteral( "无法打开解码器: " ) + Decode::ffmpegError( ret ), RtspOverallError::Other ) );
		return false;
	}

	// 分配AVPacket和AVFrame
	m_packet.reset( av_packet_alloc() );
	if (!m_packet) {
		reportError( RtspOverallError( QStringLiteral( "无法创建AVPacket" ), RtspOverallError::Other ) );
		return false;
	}

	m_frame.reset( av_frame_alloc() );
	if (!m_frame) {
		reportError( RtspOverallError( QStringLiteral( "无法创建AVFrame" ), RtspOverallError::Other ) );
		return false;
	}

	// 读取并记录数据信息
	recordStreamInfo( stream, codec );
	return true;
}

// 记录流信息到 m_dataContext
void RtspDecodeWorker::recordStreamInfo( const AVStream* stream, const AVCodec* codec ) {
	m_dataContext.reset();
	// 防止返回nullptr
	const auto text = [ ]( const char* value, const QString& fallback ) {
		return value ? QString::fromUtf8( value ) : fallback;
		};

	if (m_formatContext && m_formatContext->iformat) {
		m_dataContext.containerFormat = text( m_formatContext->iformat->name, QStringLiteral( "unknown" ) );
		m_dataContext.formatName = text( m_formatContext->iformat->long_name, m_dataContext.containerFormat );
	}
	m_dataContext.codecName = text( codec ? codec->name : nullptr, QStringLiteral( "unknown" ) );

	const AVCodecParameters* params = stream->codecpar;
	AVPixelFormat pixelFormat = m_codecContext ? m_codecContext->pix_fmt : AV_PIX_FMT_NONE;
	if (pixelFormat == AV_PIX_FMT_NONE && params)
		pixelFormat = static_cast<AVPixelFormat>(params->format);
	m_dataContext.pixelFormat = text( av_get_pix_fmt_name( pixelFormat ), QStringLiteral( "unknown" ) );

	m_dataContext.colorSpace = text( av_color_space_name( params->color_space ), QStringLiteral( "unspecified" ) );
	m_dataContext.color_range = text( av_color_range_name( params->color_range ), QStringLiteral( "unspecified" ) );

	m_dataContext.timeBase = av_q2d( stream->time_base );
	m_dataContext.width = m_codecContext ? m_codecContext->width : params->width;
	m_dataContext.height = m_codecContext ? m_codecContext->height : params->height;
	m_dataContext.videoStreamIndex = stream->index;

	// avg_frame_rate 对 RTSP 常常是 0/0，退回 r_frame_rate
	// av_q2d 在分母为 0 时会返回 inf/nan，转 int 是未定义行为
	AVRational frameRate = stream->avg_frame_rate;
	if (frameRate.num <= 0 || frameRate.den <= 0)
		frameRate = stream->r_frame_rate;
	double fps = 0.0;
	if (frameRate.num > 0 && frameRate.den > 0)
		fps = av_q2d( frameRate );
	if (!std::isfinite( fps ) || fps <= 0.0)
		fps = 0.0;
	m_dataContext.fps = static_cast<int>(std::lround( fps ));
}

// 读取并解码循环(工作线程进行调用)
bool RtspDecodeWorker::readAndDecodeLoop() {
	bool cleanEof = false; // 记录正常结束
	bool failed = false; // 记录失败

	// 开始解码循环
	while (!isStopRequested()) {
		// 每次读取前重置超时计时，超时约束的是“单次阻塞读”，而不是整个会话
		m_interruptContext.restart();
		// 读取Packet压缩包
		const int readRet = av_read_frame( m_formatContext.get(), m_packet.get() );
		if (readRet < 0) {
			if (readRet == AVERROR_EOF) {
				cleanEof = true;
			}
			else if (isStopRequested()) {
				// 停止请求通过 interrupt_callback 让 av_read_frame 返回 AVERROR_EXIT
			}
			// 如果超时返回，av_read_frame 会返回 AVERROR_EXIT，表示中断回调触发了退出
			else if (readRet == AVERROR_EXIT) {
				reportError( RtspOverallError::ReadTimeoutError(
					QStringLiteral( "读取数据包超时" ) ) );
				failed = true;
			}
			else {
				reportError( RtspOverallError( QStringLiteral( "读取数据包失败: " ) + Decode::ffmpegError( readRet ), RtspOverallError::Other ) );
				failed = true;
			}
			break;
		}

		// 非视频 packet 必须及时 unref，不能继续往下送进视频解码器
		if (m_packet->stream_index != m_videoStreamIndex) {
			av_packet_unref( m_packet.get() );
			continue;
		}
		// 发送给解码器进行解码
		int sendRet = avcodec_send_packet( m_codecContext.get(), m_packet.get() );
		if (sendRet == AVERROR(EAGAIN)) {
			// 解码器输入队列满
			// 先把输出帧收完（receive），再重发同一个 packet
			if (!receiveFrames()) {
				av_packet_unref( m_packet.get() );
				failed = true;
				break;
			}
			// 收完后重新发送packet
			sendRet = avcodec_send_packet( m_codecContext.get(), m_packet.get() );
		}
		av_packet_unref( m_packet.get() );	// 释放计数
		// 出现错误，返回错误
		if (sendRet < 0 && sendRet != AVERROR_EOF) {
			reportError( RtspOverallError( QStringLiteral( "发送数据包失败: " ) + Decode::ffmpegError( sendRet ), RtspOverallError::Other ) );
			failed = true;
			break;
		}
		// 发送后在接收解码器的输出帧
		if (!receiveFrames()) {
			failed = true;
			break;
		}
	}

	// 只有正常 EOF 才需要 flush 解码器内部缓存的帧；主动 stop 时跳过，保证能快速退出
	if (cleanEof && !isStopRequested()) {
		avcodec_send_packet( m_codecContext.get(), nullptr );
		receiveFrames();
		emit streamFinished();
	}

	return !failed;
}

// 循环接收帧
bool RtspDecodeWorker::receiveFrames() {
	while (true) {
		// 如果暂停，取消
		if (isStopRequested())
			return true;
		 // 接收帧
		const int ret = avcodec_receive_frame( m_codecContext.get(), m_frame.get() );
		// 如果等待后续的packet或者冲刷完，返回true
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
			// EAGAIN: 当前 packet 暂时没有更多帧，等下一个 packet
			// EOF: 解码器已经冲刷完
			return true;
		}
		// 解码失败
		if (ret < 0) {
			reportError( RtspOverallError( QStringLiteral( "解码帧失败: " ) + Decode::ffmpegError( ret ), RtspOverallError::Other ) );
			av_frame_unref( m_frame.get() );
			return false;
		}

		emitDecodedFrame();
		av_frame_unref( m_frame.get() );
	}
}

// 转换帧为QImage
QImage RtspDecodeWorker::convertFrame( const AVFrame* frame ) {
	if (!frame || frame->width <= 0 || frame->height <= 0)
		return {};

	const AVPixelFormat sourceFormat = static_cast<AVPixelFormat>(frame->format);
	if (sourceFormat == AV_PIX_FMT_NONE) {
		emit errorOccurred( RtspOverallError( QStringLiteral( "不支持的像素格式" ), RtspOverallError::Other ) );
		return {};
	}

	// 使用 cached context：分辨率或像素格式变化（RTSP 重连后可能发生）时
	// 它会自动释放旧上下文并重建，避免继续用旧参数转换导致花屏。
	m_swsContext = sws_getCachedContext(
		m_swsContext,
		frame->width, frame->height, sourceFormat,
		frame->width, frame->height, AV_PIX_FMT_RGB32,
		SWS_BILINEAR, nullptr, nullptr, nullptr );
	if (!m_swsContext) {
		emit errorOccurred( RtspOverallError( QStringLiteral( "无法创建像素格式转换上下文" ), RtspOverallError::Other ) );
		return {};
	}

	// Format_RGB32 在内存中的布局与 AV_PIX_FMT_RGB32 一致（小端：B,G,R,A）
	QImage image( frame->width, frame->height, QImage::Format_RGB32 );
	if (image.isNull()) {
		emit errorOccurred( RtspOverallError( QStringLiteral( "无法分配QImage" ), RtspOverallError::Other ) );
		return {};
	}

	uint8_t* destinationData[4] = { image.bits(), nullptr, nullptr, nullptr };
	int destinationLinesize[4] = { static_cast<int>(image.bytesPerLine()), 0, 0, 0 };

	const int scaled = sws_scale(
		m_swsContext,
		frame->data, frame->linesize, 0, frame->height,
		destinationData, destinationLinesize );
	if (scaled <= 0) {
		emit errorOccurred( RtspOverallError( QStringLiteral( "像素转换失败" ), RtspOverallError::Other ) );
		return {};
	}

	return image;
}

// 发送解码后的帧（QImage）
void RtspDecodeWorker::emitDecodedFrame() {
	QImage image = convertFrame( m_frame.get() );
	if (image.isNull())
		return;

	const quint64 nowMs = static_cast<quint64>(av_gettime_relative() / 1000);
	// 填写最新帧的信息(帧、索引、pts等)
	LastestFrameStore store;
	store.lastestFrame = std::move( image );
	store.frameIndex = m_decodedFrameIndex++;

	const int64_t pts = m_frame->best_effort_timestamp;
	store.pts = (pts == AV_NOPTS_VALUE) ? -1 : pts;
	if (pts == AV_NOPTS_VALUE || m_videoStreamIndex < 0) {
		store.ptsSeconds = -1.0;
	}
	else {
		store.ptsSeconds = pts * av_q2d( m_formatContext->streams[m_videoStreamIndex]->time_base );
	}

	// 第一版还没拆出“读取时间/解码时间”，先都填当前单调时钟，后续再细化
	store.receivedTime = nowMs;
	store.decodedTime = nowMs;

	// 第一帧成功解码并发出，进入 Playing
	if (store.frameIndex == 0) {
		emit statusChanged( RtspStatus::Playing );
	}
	emit frameDecoded( store );
}

// 停止解码
void RtspDecodeWorker::stopDecoding() {
	requestStop();
}

// 暂停解码
void RtspDecodeWorker::pauseDecoding() {
	m_running.store(false,std::memory_order_relaxed);
	m_interruptContext.abort.store( true, std::memory_order_relaxed );
	emit statusChanged( RtspStatus::Paused );
}

// 恢复解码
void RtspDecodeWorker::resumeDecoding() {
	m_running.store(true,std::memory_order_relaxed);
	m_interruptContext.abort.store( false, std::memory_order_relaxed );
	emit statusChanged( RtspStatus::Playing );
}
