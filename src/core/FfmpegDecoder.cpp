#include "FfmpegDecoder.h"
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
	m_config = config;
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

// 借用帧指针
const AVFrame* FfmpegDecoder::getCurrentFrame() const {
	return m_frame.get();
}
DecodeStatistics FfmpegDecoder::getStatistics() const {
	return m_decodeStatistics;
}
// 清理所有资源
void FfmpegDecoder::releaseResources() {
	m_frame.reset();
	m_packet.reset();
	m_codecContext.reset();
	m_formatContext.reset();
}
// 清除所有资源以及重置配置和统计信息等
void  FfmpegDecoder::reset() {
	releaseResources();
	m_videoStreamIndex = -1;
	m_config = DecodeConfig();
	m_streamInfo = StreamInfo();
	m_lastError = DecodeError::NoError();
	m_decodeStatistics.reset();
}

// 打开输入(获取formatContext,设置回调和选项，查找流信息)
bool FfmpegDecoder::openInput() {
	// 如果已经打开，直接返回
	if (isOpened()) {
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
	av_dict_set_int( &options, "timeout", static_cast<int64_t>(m_interruptContex.timeoutMs) * 1000, 0 );	 // 设置超时时间(us)
	// 设置其他选项，例如 rtsp_transport
	for (const auto& [key, value] : m_config.options) {
		if (int optRet = av_dict_set( &options, key.toUtf8().constData(), value.toUtf8().constData(), 0 ); optRet < 0) {
			m_lastError = DecodeError::OpenFailedError( QStringLiteral( "设置选项失败，错误码: %1 选项：%2 值：%3" ).arg( FfmpegDecode::ffmepegError( optRet ) ).arg( key ).arg( value ) );
			av_dict_free( &options );
			return false;
		}
	}
	// 打开输入
	int openRet = avformat_open_input( &rawFormatContext, m_config.url.toString().toUtf8().constData(), nullptr, &options );
	av_dict_free( &options );
	if (openRet < 0) {
		m_lastError = DecodeError::OpenFailedError( QStringLiteral( "无法打开输入: %1" ).arg( FfmpegDecode::ffmepegError( openRet ) ) );
		// 注意：avformat_open_input 失败时会自行释放 context，并把指针置为 nullptr
		if (isStopRequested()) // 其他县城请求停止，直接返回
			return false;
		if (m_interruptContex.isTimeout()) {
			m_lastError = DecodeError::ReadTimeoutError( QStringLiteral( "打开输入超时: %1" ).arg( m_config.url.toString() ) );
		}
		else {
			m_lastError = DecodeError::OpenFailedError( QStringLiteral( "无法打开输入: %1" ).arg( FfmpegDecode::ffmepegError( openRet ) ) );
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
			m_lastError = DecodeError::ReadTimeoutError( QStringLiteral( "查找流信息超时: %1" ).arg( m_config.url.toString() ) );
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
	m_videoStreamIndex = av_find_best_stream( m_formatContext.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0 );
	if (m_videoStreamIndex < 0) {
		m_lastError = DecodeError::StreamNotFoundError( QStringLiteral( "无法找到视频流: %1" ).arg( FfmpegDecode::ffmepegError( m_videoStreamIndex ) ) );
		return false;
	}
	// 查找解码器
	const AVCodec* codec = avcodec_find_decoder( m_formatContext->streams[m_videoStreamIndex]->codecpar->codec_id );
	if (!codec) {
		m_lastError = DecodeError::StreamNotFoundError( QStringLiteral( "无法找到解码器" ) );
		return false;
	}
	// 根据视频流创建解码器上下文
	m_codecContext.reset( avcodec_alloc_context3( codec ) );
	if (!m_codecContext) {
		m_lastError = DecodeError::OpenFailedError( QStringLiteral( "无法创建解码器上下文" ) );
		return false;
	}
	// 获取视频流
	AVStream* stream = m_formatContext->streams[m_videoStreamIndex];
	if (int ret = avcodec_parameters_to_context( m_codecContext.get(), stream->codecpar ); ret < 0) {
		m_lastError = DecodeError::OpenFailedError( QStringLiteral( "无法初始化解码器上下文: %1" ).arg( FfmpegDecode::ffmepegError( ret ) ) );
		return false;
	}
	// 开启解码器
	if (int ret = avcodec_open2( m_codecContext.get(), codec, nullptr ); ret < 0) {
		m_lastError = DecodeError::OpenFailedError( QStringLiteral( "无法打开解码器: %1" ).arg( FfmpegDecode::ffmepegError( ret ) ) );
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

// 读取packet并解码，阻塞直到停止、EOF或出错
bool FfmpegDecoder::readAndDecodeLoop() {
	bool failed = false;  // 失败
	bool cleanEof = false;		   // 是否正常结束
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
				m_lastError = DecodeError::ReadFailedError( QStringLiteral( "读取数据包失败: %1" ).arg( FfmpegDecode::ffmepegError( readRet ) ) );
				failed = true;
			}
			break;
		}

		// 记录读取统计等信息
		++m_decodeStatistics.receivedPacketCount;
		m_decodeStatistics.lastReadTime = readTimeUs;
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
		int sendRet = avcodec_send_packet( m_codecContext.get(), m_packet.get() );
		if (sendRet == AVERROR( EAGAIN )) {
			// 解码器输入队列满，先收帧再重发同一个packet
			if (!receiveFrames()) {
				av_packet_unref( m_packet.get() );
				failed = true;
				break;
			}
			sendRet = avcodec_send_packet( m_codecContext.get(), m_packet.get() );
		}
		av_packet_unref( m_packet.get() );	// 释放计数
		// 出现错误，返回错误
		if (sendRet < 0 && sendRet != AVERROR_EOF) {
			m_lastError = DecodeError::ReadFailedError( QStringLiteral( "发送数据包失败: %1" ).arg( FfmpegDecode::ffmepegError( sendRet ) ) );
			failed = true;
			break;
		}
		// 发送后在接收解码器的输出帧
		if (!receiveFrames()) {
			failed = true;
			break;
		}
		// 正常EOF时需要flush解码器内部缓存的帧
		if (cleanEof && !isStopRequested()) {
			avcodec_send_packet( m_codecContext.get(), nullptr );
			receiveFrames();
		}
		return !failed;
	}
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
			m_lastError = DecodeError::ReadFailedError( QStringLiteral( "解码帧失败: %1" ).arg( FfmpegDecode::ffmepegError( ret ) ) );
			av_frame_unref( m_frame.get() );
			return false;
		}
		// 记录解码统计
		++m_decodeStatistics.decodedFrameCount;
		const int64_t pts = m_frame->best_effort_timestamp;
		m_decodeStatistics.lastPts = (pts == AV_NOPTS_VALUE) ? -1 : pts;
	}
}

// 

// 打开
bool FfmpegDecoder::open() {
	// 如果已经打开，直接返回
	bool expected = false;
	if (!m_running.compare_exchange_strong( expected, true, std::memory_order_acq_rel )) {
		return true;
	}
	// 复位所有标志位
	m_stopRequested.store( false, std::memory_order_relaxed );
	m_interruptContex.resetStatus();
	m_decodeStatistics.reset();
	m_lastError = DecodeError::NoError();
	m_decodeIndex = 0;

	// 校验当前的URL有效
	if (m_config.url.isEmpty() || !m_config.url.isValid()) {
		m_lastError = DecodeError::OpenFailedError( QStringLiteral( "URL无效: %1" ).arg( m_config.url.toString() ) );
		return false;
	}
	// 校验配置
	if (!m_config.isValid()) {
		m_lastError = DecodeError::OpenFailedError( QStringLiteral( "配置无效" ) );
		return false;
	}
	// 初始化
	static std::once_flag networkInitOnce;
	std::call_once( networkInitOnce, [ ] {
		avformat_network_init();
		} );

	const bool OK = openInput && initDecoder();

	return OK;
}

// 读取下一帧
ReadStatus FfmpegDecoder::readNextFrame() {
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
		m_decodeStatistics.lastDecodeTime = av_gettime();
		if (ret == 0) {
			return ReadStatus::FrameReady;
		}
		if (ret < 0) {
			if (ret == AVERROR_EOF) {
				return ReadStatus::EndOfFile;
			}
			if (ret != AVERROR( EAGAIN )) {
				m_lastError = DecodeError::ReadFailedError( QStringLiteral( "解码帧失败: %1" ).arg( FfmpegDecode::ffmepegError( ret ) ) );
				return ReadStatus::Error;
			}
		}
		// 如果没有解码出来的帧，则读取新的packet并解码
		if (m_packet) {	  // 如果当前还有packet没有发送完，发送
			ret = avcodec_send_packet( m_codecContext.get(), m_packet.get() );
			if (ret == AVERROR( EAGAIN ))continue;
			av_packet_unref( m_packet.get() );
			m_packet.reset();
			continue;
		}
		// 如果已经flush过EOF，则直接返回EndOfFile
		if (flushEof) {
			return ReadStatus::EndOfFile;
		}

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
			m_lastError = DecodeError::ReadFailedError( QStringLiteral( "读取数据包失败: %1" ).arg( FfmpegDecode::ffmepegError( ret ) ) );
			return ReadStatus::Error;
		}

		// 获取读取时间戳
		m_decodeStatistics.lastReadTime = av_gettime();
		// 非视频packet及时unref
		if (m_packet->stream_index != m_videoStreamIndex) {
			av_packet_unref( m_packet.get() );
			continue;
		}
	}

}