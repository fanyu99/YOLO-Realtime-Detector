#include "RtspStreamController.h"

#include <QMetaEnum>
#include <QRandomGenerator>
#include <QtMath>

namespace {
	// 帧通知上限约 60fps：解码帧率可能高于屏幕刷新率，
	// 逐帧通知会让 QML 的绑定和 Image 重取图压满 GUI 线程。
	constexpr int kFrameNotifyIntervalMs = 16;
}

// ===== 构造 / 析构 =====

RtspStreamController::RtspStreamController( QObject* parent )
	: QObject( parent )
{
	qRegisterMetaType<RtspStatus>( "RtspStatus" );
	qRegisterMetaType<RtspOverallError>( "RtspOverallError" );
	qRegisterMetaType<LastestFrameStore>( "LastestFrameStore" );
	qRegisterMetaType<RtspConfig>( "RtspConfig" );
	qRegisterMetaType<RtspDataContext>( "RtspDataContext" );
	qRegisterMetaType<RtspStreamStatus>( "RtspStreamStatus" );

	// 重连退避定时器：单发，到期后重启worker
	m_retryTimer.setSingleShot( true );
	connect( &m_retryTimer, &QTimer::timeout, this, &RtspStreamController::onRetryTimeout );

	// 连续设置配置只重启一次
	m_restartTimer.setSingleShot( true );
	m_restartTimer.setInterval( 150 );
	connect( &m_restartTimer, &QTimer::timeout, this, &RtspStreamController::onRestartTimeout );


}

RtspStreamController::RtspStreamController( const RtspConfig& config, QObject* parent )
	: RtspStreamController( parent )
{
	m_inputConfig = config;
	m_rtspUrl = config.url;
}

RtspStreamController::~RtspStreamController() {
	releaseResources();
}

// ===== worker 生命周期 =====

// 懒加载创建 worker + 线程，只创建一次；并链接信号槽
void RtspStreamController::ensureWorkerCreated() {
	if (m_worker && m_thread)
		return;

	if (!m_thread)
		m_thread = std::make_unique<QThread>();
	// 第一次才创建worker对象，后续直接复用worker
	if (!m_worker) {
		m_worker = std::make_unique<RtspDecodeWorker>();
		m_worker->moveToThread( m_thread.get() );

		// 状态转发：Worker(解码线程) -> Controller(缓存) -> QML(绑定)
		connect( m_worker.get(), &RtspDecodeWorker::StatusChanged,
			this, &RtspStreamController::onWorkerStatusChanged );
		// 错误转发（只缓存 + 转发）
		connect( m_worker.get(), &RtspDecodeWorker::ErrorOccurred,
			this, &RtspStreamController::onWorkerError );
		// 会话结束：重连
		connect( m_worker.get(), &RtspDecodeWorker::Finished,
			this, &RtspStreamController::onWorkerFinished );
		// 帧转发并缓存：信号通知 QML 限流到60fps
		connect( m_worker.get(), &RtspDecodeWorker::FrameDecoded, this, [ this ]( const LastestFrameStore& newFrame ) {
			m_lastestFrameStore = newFrame;
			if (m_frameNotifyClock.isValid() && m_frameNotifyClock.elapsed() < kFrameNotifyIntervalMs)
				return;
			m_frameNotifyClock.restart();
			++m_frameRevision;
			emit FrameUpdated( newFrame );
			} );
		// 流信息更新：缓存新流信息并通知QML进行更新
		connect( m_worker.get(), &RtspDecodeWorker::DataContextChanged, this, [ this ]( const RtspDataContext& context ) {
			m_dataContext = context;
			emit DataContextChanged( m_dataContext );
			} );
		// 统计信息更新：缓存Worker上报的计数与帧率，状态和重连次数由Controller填写
		connect( m_worker.get(), &RtspDecodeWorker::StreamStatusChanged, this, [ this ]( const RtspStreamStatus& status ) {
			const int reconnectCount = m_retryCount;
			const RtspStatus currentStatus = m_currentStatus;
			m_streamStatus = status;
			m_streamStatus.reconnectCount = reconnectCount;
			m_streamStatus.currentStatus = currentStatus;
			emit StreamStatusChanged( m_streamStatus );
			} );
	}
}

// 启动一次解码会话：设置worker配置 + 解码线程
void RtspStreamController::launchWorker() {
	// 懒加载创建worker + 线程
	ensureWorkerCreated();

	if (!m_thread->isRunning())
		m_thread->start();

	m_phase = RetryProgress::Streaming;
	m_pendingRestart = false;
	m_retryTimer.stop();
	setStatus( RtspStatus::Opening );

	// 先应用配置，再启动
	const RtspConfig config = m_inputConfig;
	QMetaObject::invokeMethod( m_worker.get(), [ worker = m_worker.get(), config ] {
		worker->setRtspConfig( config );
		}, Qt::QueuedConnection );
	QMetaObject::invokeMethod( m_worker.get(), &RtspDecodeWorker::startDecoding, Qt::QueuedConnection );
}

// ===== 重连状态机 =====

// 判断是否能够重连
bool RtspStreamController::isRetryable( RtspOverallError::ErrorCode code ) {
	switch (code) {
		// 临时故障：可重连
	case RtspOverallError::ConnectionFailed:
	case RtspOverallError::ReadTimeout:
	case RtspOverallError::StreamFailed:
	case RtspOverallError::Other:
		return true;
		// 配置/URL等错误，不重连
	default:
		return false;
	}
}

// 计算第attempt次的退避时长，防止频繁重连
int RtspStreamController::backoffMs( int attempt ) {
	const int base = 500;
	const int cap = 15000;
	const int shift = qBound( 0, attempt - 1, 5 );
	int delay = qMin( cap, base << shift );
	delay += QRandomGenerator::global()->bounded( delay / 4 + 1 );
	return delay;
}
// 规划重连：
// 如果超过最大次数，放弃重连
// 否则等待重连，更新退避计时和重连次数
void RtspStreamController::scheduleRetry() {
	// 重连超限
	if (m_retryCount >= m_inputConfig.maxReconnect) {
		giveUp( ReconnectEndReason::OverLimit );
		return;
	}
	++m_retryCount;
	m_phase = RetryProgress::WaitingRetry;
	m_streamStatus.reconnectCount = m_retryCount;
	setStatus( RtspStatus::Reconnecting );
	emit RetryCountChanged();
	emit Reconnecting();
	emit ReconnectAttempt( m_retryCount, m_inputConfig.maxReconnect );
	m_retryTimer.start( backoffMs( m_retryCount ) );
}

// 退避时间到，重连
void RtspStreamController::onRetryTimeout() {
	if (m_phase != RetryProgress::WaitingRetry)
		return;
	// 重连到时，重启
	launchWorker();
}

// 重连出现问题，放弃重连
// 再进行转发最后的错误以及结束原因
void RtspStreamController::giveUp( ReconnectEndReason reason ) {
	m_phase = RetryProgress::Failed;
	m_retryTimer.stop();
	auto reasonStr = [ reason ]() {
		switch (reason) {
		case ReconnectEndReason::None:
			return QStringLiteral( "无" );
		case ReconnectEndReason::NoRetryable:
			return QStringLiteral( "不可重连的错误" );
		case ReconnectEndReason::OverLimit:
			return QStringLiteral( "超过最大重连次数" );
		case ReconnectEndReason::CanceledByUser:
			return QStringLiteral( "用户主动关闭自动重连" );
		case ReconnectEndReason::AutoReconnectClosed:
			return QStringLiteral( "未开启自动重连" );
		default:
			return QStringLiteral( "未知原因" );
		}
		};
	m_currentError = RtspOverallError( QStringLiteral( "%1 %2" ).arg( reasonStr(), m_currentError.errorMessage ), RtspOverallError::ReconnectFailed );
	setStatus( RtspStatus::Error );
	emit ReconnectFailed( m_retryCount, m_currentError, reason );
	emit ErrorOccurred( m_currentError );
}

// ===== worker 回调 =====

void RtspStreamController::onWorkerStatusChanged( RtspStatus status ) {
	if (m_phase == RetryProgress::WaitingRetry)
		return;

	switch (status) {
	case RtspStatus::Opened:
		setStatus( status );
		break;
	case RtspStatus::Playing:
		m_retryCount = 0; // 成功,复位重连计数
		m_streamStatus.reconnectCount = 0;
		emit RetryCountChanged();
		setStatus( status );
		break;
	case RtspStatus::Paused:
		setStatus( status );
		break;
	default:
		setStatus( status );
		break;
	}
}

// 转发并缓存错误
void RtspStreamController::onWorkerError( const RtspOverallError& error ) {
	m_currentError = error;
	emit ErrorOccurred( error );
}

// worker结束，判断是否出现错误
void RtspStreamController::onWorkerFinished( bool clean, const RtspOverallError& error ) {
	// 配置变更引起的主动停止：立即用新配置重启
	if (m_pendingRestart) {
		m_pendingRestart = false;	// 复位
		launchWorker();
		return;
	}
	// 用户主动停止（置为Idle）
	if (m_phase == RetryProgress::Idle) {
		setStatus( RtspStatus::Stopped );
		return;
	}
	// 正常 EOF：不重连，回到空闲
	if (clean) {
		m_phase = RetryProgress::Idle;
		m_retryCount = 0;
		m_streamStatus.reconnectCount = 0;
		setStatus( RtspStatus::Stopped );
		emit RetryCountChanged();
		return;
	}
	// 出错：判断是否重连
	if (!m_inputConfig.autoReconnect) {  // 1.没有开启自动关闭
		m_currentError = error;
		giveUp( ReconnectEndReason::AutoReconnectClosed );
		return;
	}
	if (!isRetryable( error.errorCode )) { // 2.不可重连的错误，直接进入终态
		m_currentError = error;
		giveUp( ReconnectEndReason::NoRetryable );
		return;
	}
	// 能够重连，安排一次重连
	scheduleRetry();
}

// ===== 对外接口 =====

// 开始拉流
bool RtspStreamController::start() {
	if (m_phase == RetryProgress::Streaming || m_phase == RetryProgress::WaitingRetry) {
		return true; // 已在拉流 / 等待重连
	}
	if (!m_inputConfig.isValid()) {
		onWorkerError( RtspOverallError::InvalidConfigError( QStringLiteral( "无效配置，无法开始拉流" ) ) );
		return false;
	}

	// 直接 start 时取消 setUrl/setInputConfig 留下的防抖重启，
	// 否则 150ms 后会把刚启动的会话再停一次
	m_restartTimer.stop();

	// 复位状态信息

	m_currentError = RtspOverallError{};
	m_retryCount = 0;
	m_streamStatus.reconnectCount = 0;
	m_pendingRestart = false;


	emit RetryCountChanged();
	emit ErrorOccurred( m_currentError ); // 通知刷新错误信息为空
	// 启动Worker
	launchWorker();

	return true;
}

// 主动停止
void RtspStreamController::stop() {
	const bool wasWaitingRetry = (m_phase == RetryProgress::WaitingRetry);
	const bool retryFailed = (m_phase == RetryProgress::Failed);

	m_retryTimer.stop();
	m_pendingRestart = false;
	m_phase = RetryProgress::Idle;

	if (m_worker && m_worker->isRunning()) {
		setStatus( RtspStatus::Stopping );
		m_worker->requestStop(); // 直接调用而不是采用信号机制，避免阻塞
	}
	else if (wasWaitingRetry || !m_worker || retryFailed) {
		setStatus( RtspStatus::Stopped );
	}
}

// 暂停取流
void RtspStreamController::pause() {
	if (m_worker && m_worker->isRunning() && m_currentStatus == RtspStatus::Playing) {
		// 直接内联调用，避免队列调用在解码线程阻塞
		m_worker->pauseDecoding();
	}
}

// 恢复取流
bool RtspStreamController::resume() {
	if (m_worker && m_currentStatus == RtspStatus::Paused) {
		// 直接内联调用，避免队列调用在解码线程阻塞
		m_worker->resumeDecoding();
		return true;
	}
	onWorkerError( RtspOverallError{ QStringLiteral( "恢复拉流失败！当前状态:" ) + EnumText::toString( m_currentStatus ), RtspOverallError::Other } );
	return false;
}

// 清理所有资源：销毁Worker并 停止工作线程
void RtspStreamController::releaseResources() {
	m_retryTimer.stop();
	m_restartTimer.stop();
	if (m_worker)
		m_worker->requestStop();
	if (m_thread) {
		m_thread->quit();
		m_thread->wait();
	}
	// 先停线程，再销毁 worker
	m_worker.reset();
	m_thread.reset();
	m_phase = RetryProgress::Idle;
}

// ===== 属性 =====

// 是否在拉流
bool RtspStreamController::isStreaming() const {
	return m_worker && m_worker->isRunning();
}

// 当前的状态
RtspStatus RtspStreamController::currentStatus() const {
	return m_currentStatus;
}

// 当前错误
RtspOverallError RtspStreamController::currentError() const {
	return m_currentError;
}

// 设置状态
void RtspStreamController::setStatus( RtspStatus status ) {
	if (m_currentStatus == status) // 防止重复发射信号
		return;
	m_currentStatus = status;
	m_streamStatus.currentStatus = status;
	emit StatusChanged( status );
	emit StreamStatusChanged( m_streamStatus );
}
// 是否/设置自动重连
bool RtspStreamController::autoReconnect() const {
	return m_inputConfig.autoReconnect;
}

void RtspStreamController::setAutoReconnect( bool autoReconnect ) {
	if (m_inputConfig.autoReconnect == autoReconnect)
		return;
	m_inputConfig.autoReconnect = autoReconnect;
	if (!autoReconnect && m_phase == RetryProgress::WaitingRetry)   // 如果自动重连关闭，取消正在等待的重连进度
	{
		// 如果正在等待重连，直接放弃重连，进入终态
		giveUp( ReconnectEndReason::CanceledByUser );
	}
	emit AutoReconnectChanged();
}

// 最大重连次数
int RtspStreamController::maxReconnect() const {
	return m_inputConfig.maxReconnect;
}


void RtspStreamController::setMaxReconnect( int maxReconnect ) {
	if (m_inputConfig.maxReconnect == maxReconnect)
		return;
	m_inputConfig.maxReconnect = qMax( 0, maxReconnect );
	emit AutoReconnectChanged();
}

// 当前的重连次数
int RtspStreamController::retryCount() const {
	return m_retryCount;
}

// 当前的拉流统计信息
RtspStreamStatus RtspStreamController::streamStatus() const {
	return m_streamStatus;
}


// URL
QUrl RtspStreamController::getUrl() const {
	return m_rtspUrl;
}

void RtspStreamController::setUrl( const QUrl& url ) {
	if (m_rtspUrl == url)
		return;
	m_rtspUrl = url;
	m_inputConfig.url = url;
	emit RtspUrlChanged();
	m_restartTimer.start();
}

// RTSP配置
RtspConfig RtspStreamController::inputConfig() const {
	return m_inputConfig;
}

void RtspStreamController::setInputConfig( const RtspConfig& config ) {
	const bool urlChanged = (config.url != m_inputConfig.url);
	const bool streamChanged = m_inputConfig.isStreamConfigEqual( config ) == false;
	const bool autoReconnectChanged = (config.autoReconnect != m_inputConfig.autoReconnect);
	const bool maxReconnectChanged = (config.maxReconnect != m_inputConfig.maxReconnect);
	if (urlChanged || streamChanged || autoReconnectChanged || maxReconnectChanged)
	{
		m_inputConfig = config;
		m_rtspUrl = config.url;
	}
	// 只有变更时才发送信号
	if (urlChanged)
		emit RtspUrlChanged();
	if (streamChanged || maxReconnectChanged || autoReconnectChanged)
		emit InputConfigChanged();
	// 自动重连变更才进行更改和发送信号
	if (autoReconnectChanged || maxReconnectChanged) {
		// 关闭自动重连，停掉重连
		if (autoReconnectChanged && !config.autoReconnect && m_phase == RetryProgress::WaitingRetry)
			giveUp( ReconnectEndReason::CanceledByUser );
		emit AutoReconnectChanged();
	}

	if (streamChanged)	  // 流更改，需要重新启动拉流
		m_restartTimer.start();
}

RtspDataContext RtspStreamController::dataContext() const {
	return m_dataContext;
}

LastestFrameStore RtspStreamController::lastestFrameStore() const {
	return m_lastestFrameStore;
}

int RtspStreamController::frameRevision() const {
	return m_frameRevision;
}

QString RtspStreamController::statusText() const {
	return EnumText::toString(m_currentStatus);
}

// 配置变更防抖到期：只重启流
void RtspStreamController::onRestartTimeout() {
	if (m_phase == RetryProgress::Idle || m_phase == RetryProgress::Failed) {
		if (m_inputConfig.isValid())
			launchWorker();  // 未在拉流：直接按新配置启动
		return;
	}
	if (m_phase == RetryProgress::WaitingRetry) {
		launchWorker();      // 正在等待重连：立即用新配置重启
		return;
	}
	// 正在拉流：标记待重启并请求停止，等 worker 退出后由 onWorkerFinished 重启
	m_pendingRestart = true;
	if (m_worker) {
		setStatus( RtspStatus::Stopping );
		m_worker->requestStop();
	}
}
