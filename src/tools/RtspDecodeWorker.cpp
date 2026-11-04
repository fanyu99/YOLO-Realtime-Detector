#include "RtspDecodeWorker.h"

#include <cmath>

namespace {
	// 统计信息的上报间隔(ms)
	constexpr qint64 STATUS_REPORT_INTERVAL_MS = 1000;
}

// ===== RtspDecodeWorker =====
RtspDecodeWorker::RtspDecodeWorker( QObject* parent )
	: QObject( parent )
{
	qRegisterMetaType<RtspStatus>( "RtspStatus" );
	qRegisterMetaType<RtspOverallError>( "RtspOverallError" );
	qRegisterMetaType<LastestFrameStore>( "LastestFrameStore" );
	qRegisterMetaType<RtspStreamStatus>( "RtspStreamStatus" );
}

RtspDecodeWorker::RtspDecodeWorker( const RtspConfig& config, QObject* parent )
	: RtspDecodeWorker( parent )
{
	m_config = config;
	m_rtspUrl = config.url;
}

RtspDecodeWorker::~RtspDecodeWorker() {
	this->reset();
}

// 统一上报错误：只发 errorOccurred 并记录最后一次错误，
void RtspDecodeWorker::reportError( const RtspOverallError& error ) {
	m_lastError = error;
	emit ErrorOccurred( error );
}

// 清理所有资源
// 顺序：先释放只属于解码过程的 frame/packet，再释放解码器，最后关闭输入
void RtspDecodeWorker::releaseResources() {
	if (m_swsContext) {
		sws_freeContext( m_swsContext );
		m_swsContext = nullptr;
	}
	m_decoder.close();
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
// 设置RTSP配置(仅在未解码时可用，正在解码触发错误，不静默更新)
// 无效由解码时进行发出错误
// 设置后会使用新配置启动解码
void RtspDecodeWorker::setRtspConfig( const RtspConfig& config ) {
	m_config = config;
	m_rtspUrl = config.url;
}
// 设置数据上下文
void RtspDecodeWorker::setRtspDataContext( const RtspDataContext& context ) {
	m_dataContext = context;
}

// 检查是否正在工作（只要不停止就为true）
bool RtspDecodeWorker::isRunning() const {
	return m_running.load( std::memory_order_relaxed );
}

// 检查是否其他线程请求了停止
bool RtspDecodeWorker::isStopRequested() const {
	return m_stopRequested.load( std::memory_order_relaxed );
}

// 请求停止
// 必须在 GUI/控制线程里直接调用（或 Qt::DirectConnection）
// 每轮的读取循环readAndDecodeLoop()中 / 接收一帧receiveFrames中进行检查是否停止，停止直接退出循环
void RtspDecodeWorker::requestStop() {
	{
		std::lock_guard<std::mutex> lock( m_pauseMutex );
		m_stopRequested.store( true, std::memory_order_relaxed );
	}
	m_decoder.requestStop(); // 打断可能阻塞中的 av_read_frame
	m_pauseCV.notify_all(); // 唤醒阻塞进程，防止因为中断而不能够进行停止
}

// 检查是否其他线程请求了中断
bool RtspDecodeWorker::isPaused()const {
	return m_decoder.isPaused();
}

// 开始解码
// 一次调用即一次会话：open -> 解码循环 -> 释放资源 -> finished(...)
void RtspDecodeWorker::startDecoding() {
	// 校验是否正在解码中，如果是则直接返回错误
	// 防止重复 start 在同一对象上并发执行
	bool expected = false;
	if (!m_running.compare_exchange_strong( expected, true, std::memory_order_acq_rel )) {
		reportError( RtspOverallError( QStringLiteral( "解码已在运行" ), RtspOverallError::AlreadyRunning ) );
		return;
	}
	// 复位所有标志位
	// 每次启动都重置中断/停止标志
	// 否则上一次pause / stop 的 pause / stop会让这次立即退出
	m_stopRequested.store( false, std::memory_order_relaxed );
	m_decodedFrameIndex = 0;
	m_lastError = RtspOverallError{};
	emit ErrorOccurred( m_lastError ); // 通知刷新错误
	// 复位本次会话的统计信息
	m_streamStatus = RtspStreamStatus{};
	// 立即刷新一次统计信息（清空）
	emit StreamStatusChanged(m_streamStatus);
	m_lastStatusDecodedFrames = 0;
	m_lastStatusDisplayedFrames = 0;
	m_lastReadTimeUs = 0;
	m_statusClock.start();

	// 执行统一结束：无论成功、失败还是被停止，最后都发一次 finished 并复位 m_running
	// 如果clean为true,设置无错误；clean为false,设置最后一次错误
	const auto finish = [ this ]( bool clean ) {
		releaseResources();	   // 清理资源
		// 复位并发出结束信号（错误信息在错误信号发出时就保存在m_lastError）
		m_running.store( false, std::memory_order_relaxed );
		emit Finished( clean && m_lastError.errorCode == RtspOverallError::NoError, m_lastError );
		};

	// 校验URL
	if (m_rtspUrl.isEmpty() || !m_rtspUrl.isValid() || m_rtspUrl.scheme().isEmpty()) {
		reportError( RtspOverallError::InvalidUrlError( QStringLiteral( "无效的URL: " ) + m_rtspUrl.toString() ) );
		finish( false );
		return;
	}
	// 校验配置
	if (!m_config.isValid()) {
		reportError( RtspOverallError::InvalidConfigError( QStringLiteral( "无效配置" ) ) );
		finish( false );
		return;
	}

	// 组装通用解码器配置：凭据、RTSP选项和超时都由Worker提供
	QUrl openUrl = m_rtspUrl;
	if (m_config.username && !m_config.username->isEmpty() && openUrl.userName().isEmpty())
		openUrl.setUserName( *m_config.username );
	if (m_config.password && !m_config.password->isEmpty() && openUrl.password().isEmpty())
		openUrl.setPassword( *m_config.password );

	DecodeConfig decodeConfig;
	decodeConfig.url = openUrl;
	decodeConfig.connectTimeoutMs = m_config.connectTimeoutMs;
	decodeConfig.readTimeoutMs = m_config.readTimeoutMs;
	// RTSP 默认可能走 UDP，UDP 丢包不稳定且键盘中断响应差
	// 当前先固定使用TCP
	decodeConfig.options.emplace( QStringLiteral( "rtsp_transport" ), QStringLiteral( "tcp" ) );
	m_decoder.setConfig( decodeConfig );

	// 设置为打开状态
	emit StatusChanged( RtspStatus::Opening );
	// 打开输入并初始化解码器，进入读取和解码循环
	if (!m_decoder.open()) {
		reportError( toRtspError( m_decoder.getLastError() ) );
		finish( false );
		return;
	}
	// 输入已打开且流信息可用，上报 Opened
	emit StatusChanged( RtspStatus::Opened );
	recordStreamInfo( m_decoder.getStreamInfo() );

	// 打开过程中可能已经收到停止请求（decoder.open()会复位中断标志）
	if (isStopRequested()) {
		finish( true );
		return;
	}

	const bool ok = readAndDecodeLoop();
	if (!ok && !isStopRequested())
		reportError( toRtspError( m_decoder.getLastError() ) );

	// 真正的错误通过reportError发送并缓存在m_lastError中
	const bool clean = ok || isStopRequested() || m_lastError.errorCode == RtspOverallError::NoError;
	finish( clean );
}

// 记录流信息到 m_dataContext
void RtspDecodeWorker::recordStreamInfo( const StreamInfo& info ) {
	m_dataContext.reset();
	m_dataContext.containerFormat = info.containerFormat;
	m_dataContext.codecName = info.codecName;
	m_dataContext.formatName = info.formatName;
	m_dataContext.pixelFormat = info.pixelFormat;
	m_dataContext.colorSpace = info.colorSpace;
	m_dataContext.color_range = info.colorRange;
	m_dataContext.timeBase = av_q2d( info.timeBase );
	m_dataContext.width = info.width;
	m_dataContext.height = info.height;
	m_dataContext.videoStreamIndex = info.videoStreamIndex;

	// avg_frame_rate 对 RTSP 常常是 0/0，退回 r_frame_rate
	// av_q2d 在分母为 0 时会返回 inf/nan，转 int 是未定义行为
	double fps = av_q2d( info.frameRate );
	if (!std::isfinite( fps ) || fps <= 0.0)
		fps = 0.0;
	m_dataContext.fps = static_cast<int>(std::lround( fps ));
	// 发送更改信号通知controller更新流信息
	if (m_dataContext.isValid())
		emit DataContextChanged( m_dataContext );
	else
	{
		reportError( RtspOverallError( QStringLiteral( "读取的流信息无效!" ), RtspOverallError::Other ) );
		m_dataContext.reset();
	}
}

// 读取 packet 并解码，阻塞直到停止、EOF 或出错
bool RtspDecodeWorker::readAndDecodeLoop() {
	// push模型：解码器内部完成 packet 读取和解码，每解出一帧通过回调交付
	m_decoder.setFrameCallback( [ this ]( const AVFrame* frame, quint64 readTimeUs, quint64 decodedTimeUs ) {
		emitDecodedFrame( frame, readTimeUs, decodedTimeUs );
		reportStreamStatus();
		} );
	return m_decoder.startDecodingLoop();
}

// 转换帧为QImage
QImage RtspDecodeWorker::convertFrame( const AVFrame* frame ) {
	if (!frame || frame->width <= 0 || frame->height <= 0)
		return {};

	const AVPixelFormat sourceFormat = static_cast<AVPixelFormat>(frame->format);
	if (sourceFormat == AV_PIX_FMT_NONE) {
		emit ErrorOccurred( RtspOverallError( QStringLiteral( "不支持的像素格式" ), RtspOverallError::Other ) );
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
		emit ErrorOccurred( RtspOverallError( QStringLiteral( "无法创建像素格式转换上下文" ), RtspOverallError::Other ) );
		return {};
	}

	// Format_RGB32 在内存中的布局与 AV_PIX_FMT_RGB32 一致（小端：B,G,R,A）
	QImage image( frame->width, frame->height, QImage::Format_RGB32 );
	if (image.isNull()) {
		emit ErrorOccurred( RtspOverallError( QStringLiteral( "无法分配QImage" ), RtspOverallError::Other ) );
		return {};
	}

	uint8_t* destinationData[4] = { image.bits(), nullptr, nullptr, nullptr };
	int destinationLinesize[4] = { static_cast<int>(image.bytesPerLine()), 0, 0, 0 };

	const int scaled = sws_scale(
		m_swsContext,
		frame->data, frame->linesize, 0, frame->height,
		destinationData, destinationLinesize );
	if (scaled <= 0) {
		emit ErrorOccurred( RtspOverallError( QStringLiteral( "像素转换失败" ), RtspOverallError::Other ) );
		return {};
	}

	return image;
}

// 发送解码后的帧（QImage）
void RtspDecodeWorker::emitDecodedFrame( const AVFrame* frame, quint64 readTime, quint64 decodedTime ) {
	// 同步通用解码器的统计计数（packet/帧/错误等由解码器维护）
	const DecodeStatistics stats = m_decoder.getStatistics();
	m_streamStatus.receivedPacketCount = stats.receivedPacketCount;
	m_streamStatus.videoPacketCount = stats.videoPacketCount;
	m_streamStatus.decodedFrameCount = stats.decodedFrameCount;
	m_streamStatus.decodeErrorCount = stats.decodeErrorCount;
	m_streamStatus.lastPts = stats.lastPts;
	m_streamStatus.lastReadTime = stats.lastReadTime;
	m_lastReadTimeUs = stats.lastReadTime;

	QImage image = convertFrame( frame );
	if (image.isNull()) {
		++m_streamStatus.droppedFrameCount;
		return;
	}

	// 填写最新帧的信息(帧、索引、pts等)
	LastestFrameStore store;
	store.lastestFrame = std::move( image );
	store.frameIndex = m_decodedFrameIndex++;

	const int64_t pts = frame ? frame->best_effort_timestamp : AV_NOPTS_VALUE;
	store.pts = (pts == AV_NOPTS_VALUE) ? -1 : pts;
	const StreamInfo info = m_decoder.getStreamInfo();
	if (pts == AV_NOPTS_VALUE || info.timeBase.num <= 0 || info.timeBase.den <= 0) {
		store.ptsSeconds = -1.0;
	}
	else {
		store.ptsSeconds = pts * av_q2d( info.timeBase );
	}

	store.readTime = readTime;
	store.decodedTime = decodedTime;

	// 第一帧成功解码并发出，进入 Playing
	if (store.frameIndex == 0) {
		emit StatusChanged( RtspStatus::Playing );
	}
	// 发送frameDecoded
	++m_streamStatus.displayedFrameCount;
	emit FrameDecoded( store );
}

// 按固定间隔（1s）汇总并上报统计信息
void RtspDecodeWorker::reportStreamStatus() {
	if (!m_statusClock.isValid())
		m_statusClock.start();

	const qint64 elapsedMs = m_statusClock.elapsed();
	// 未到上报间隔，不发信号
	if (elapsedMs < STATUS_REPORT_INTERVAL_MS)
		return;

	const quint64 decodedDelta = m_streamStatus.decodedFrameCount - m_lastStatusDecodedFrames;
	const quint64 displayedDelta = m_streamStatus.displayedFrameCount - m_lastStatusDisplayedFrames;
	m_streamStatus.decodeFps = decodedDelta * 1000.0 / static_cast<double>( elapsedMs );
	m_streamStatus.displayFps = displayedDelta * 1000.0 / static_cast<double>( elapsedMs );
	m_lastStatusDecodedFrames = m_streamStatus.decodedFrameCount;
	m_lastStatusDisplayedFrames = m_streamStatus.displayedFrameCount;
	m_statusClock.restart();

	emit StreamStatusChanged( m_streamStatus );
}

// 停止解码
void RtspDecodeWorker::stopDecoding() {
	requestStop();
}

// 暂停解码
void RtspDecodeWorker::pauseDecoding() {
	m_decoder.setPaused( true );	// 设置中断，ffmpeg的av_read_frame内部的周期性调用回调函数interrupt_cb(详见Worker::Decode中的中断回调函数)触发中断
	emit StatusChanged( RtspStatus::Paused );
}

// 恢复解码
// 将中断上下文中的pause置为false,阻塞的工作循环会进行定期检查pause是否为true
void RtspDecodeWorker::resumeDecoding() {
	// 唤醒者与被唤醒者需要使用同一个锁进行修改标志位谓词，防止丢失唤醒
	{
		std::lock_guard<std::mutex> lock( m_pauseMutex );
		m_decoder.setPaused( false );
	}
	m_pauseCV.notify_all(); // 唤醒阻塞的readAndDecodeLoop（）循环
	emit StatusChanged( RtspStatus::Playing );

}

// 解码错误 -> RTSP 错误码
RtspOverallError RtspDecodeWorker::toRtspError( const DecodeError& error ) const {
	switch (error.errorCode) {
	case DecodeErrorCode::InvalidUrl:
		return RtspOverallError::InvalidUrlError( error.errorMessage );
	case DecodeErrorCode::InvalidConfig:
		return RtspOverallError::InvalidConfigError( error.errorMessage );
	case DecodeErrorCode::ReadTimeout:
		return RtspOverallError::ReadTimeoutError( error.errorMessage );
	case DecodeErrorCode::StreamNotFound:
	case DecodeErrorCode::CodecNotFound:
		return RtspOverallError::StreamNotFoundError( error.errorMessage );
	case DecodeErrorCode::OpenFailed:
	case DecodeErrorCode::StreamFailed:
		return RtspOverallError::ConnectionFailedError( error.errorMessage );
	case DecodeErrorCode::DecoderInitFailed:
	case DecodeErrorCode::DecodeFailed:
	case DecodeErrorCode::ReadFailed:
	case DecodeErrorCode::Other:
	case DecodeErrorCode::NoError:
	default:
		return RtspOverallError( error.errorMessage, RtspOverallError::Other );
	}
}
