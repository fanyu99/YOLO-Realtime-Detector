#include "RtspStreamController.h"

#include <QRandomGenerator>
#include <QtMath>

// ===== 构造 / 析构 =====

RtspStreamController::RtspStreamController( QObject* parent )
	: QObject( parent )
{
	qRegisterMetaType<RtspStatus>( "RtspStatus" );
	qRegisterMetaType<RtspOverallError>( "RtspOverallError" );
	qRegisterMetaType<LastestFrameStore>( "LastestFrameStore" );
	qRegisterMetaType<RtspConfig>( "RtspConfig" );
	qRegisterMetaType<RtspDataContext>( "RtspDataContext" );

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

// 懒加载创建 worker + 线程，只创建一次
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
		connect( m_worker.get(), &RtspDecodeWorker::statusChanged,
			this, &RtspStreamController::onWorkerStatusChanged );
		// 错误转发（只缓存 + 转发）
		connect( m_worker.get(), &RtspDecodeWorker::errorOccurred,
			this, &RtspStreamController::onWorkerError );
		// 会话结束：重连
		connect( m_worker.get(), &RtspDecodeWorker::finished,
			this, &RtspStreamController::onWorkerFinished );
		// 帧转发并缓存
		connect( m_worker.get(), &RtspDecodeWorker::frameDecoded, this, [ this ]( const LastestFrameStore& newFrame ) {
			m_lastestFrameStore = newFrame;
			emit FrameUpdated( newFrame.lastestFrame );
			} );
	}
}

// 启动一次解码会话：设置worker配置 + 解码线程
void RtspStreamController::launchWorker() {
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
	if (m_retryCount >= m_inputConfig.maxReconnect) {
		giveUp();
		return;
	}
	++m_retryCount;
	m_phase = RetryProgress::WaitingRetry;
	setStatus( RtspStatus::Reconnecting );
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

// 重连超限，放弃重连
// 再进行转发最后的错误
void RtspStreamController::giveUp() {
	m_phase = RetryProgress::Failed;
	m_retryTimer.stop();
	m_currentError = RtspOverallError( QStringLiteral( "重连失败：已超过最大重连次数" ),
		RtspOverallError::ReconnectFailed );
	setStatus( RtspStatus::Error );
	emit StreamClosed();
	emit ReconnectFailed( m_retryCount, m_currentError );
	emit ErrorOccurred( m_currentError );
}

// ===== worker 回调 =====

void RtspStreamController::onWorkerStatusChanged( RtspStatus status ) {
	if (m_phase == RetryProgress::WaitingRetry)
		return;

	switch (status) {
	case RtspStatus::Opened:
		setStatus( status );
		emit StreamOpened();
		break;
	case RtspStatus::Playing:
		m_retryCount = 0; // 成功,复位重连计数
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
		emit StreamClosed();
		return;
	}
	// 正常 EOF：不重连，回到空闲
	if (clean) {
		m_phase = RetryProgress::Idle;
		m_retryCount = 0;
		setStatus( RtspStatus::Stopped );
		emit StreamClosed();
		return;
	}
	// 出错：判断是否重连
	if (!m_inputConfig.autoReconnect || !isRetryable( error.errorCode )) {
		m_currentError = error;
		giveUp();
		return;
	}
	scheduleRetry();
}

// ===== 对外接口 =====

bool RtspStreamController::start() {
	if (m_phase == RetryProgress::Streaming || m_phase == RetryProgress::WaitingRetry) {
		return true; // 已在拉流 / 等待重连
	}
	if (!m_inputConfig.isValid()) {
		onWorkerError( RtspOverallError::InvalidConfigError( QStringLiteral( "无效配置，无法开始拉流" ) ) );
		return false;
	}

	m_currentError = RtspOverallError{};
	m_retryCount = 0;
	m_pendingRestart = false;
	launchWorker();
	return true;
}

// 主动停止
void RtspStreamController::stop() {
	const bool wasWaitingRetry = (m_phase == RetryProgress::WaitingRetry);

	m_retryTimer.stop();
	m_pendingRestart = false;
	m_phase = RetryProgress::Idle;

	if (m_worker && m_worker->isRunning()) {
		m_worker->requestStop(); // 直接调用而不是采用信号机制，避免阻塞
	}
	else if (wasWaitingRetry || !m_worker) {
		setStatus( RtspStatus::Stopped );
		emit StreamClosed();
	}
}

// 暂停取流
void RtspStreamController::pause() {
	if (m_worker && m_worker->isRunning() && m_currentStatus == RtspStatus::Playing) {
		m_worker->pauseDecoding();
	}
}

// 恢复取流
bool RtspStreamController::restart() {
	if (m_worker && m_currentStatus == RtspStatus::Paused ) {
		m_worker->resumeDecoding();
		return true;
	}
	onWorkerError( RtspOverallError{ QStringLiteral( "恢复拉流失败！" ), RtspOverallError::Other } );
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
	emit StatusChanged( status );
}
// 是否/设置自动重连
bool RtspStreamController::autoReconnect() const {
	return m_inputConfig.autoReconnect;
}

void RtspStreamController::setAutoReconnect( bool autoReconnect ) {
	if (m_inputConfig.autoReconnect == autoReconnect)
		return;
	m_inputConfig.autoReconnect = autoReconnect;
	if (!autoReconnect)   // 如果自动重连关闭，取消正在等待的重连进度
	{
		if (m_phase == RetryProgress::WaitingRetry) {
			m_retryTimer.stop();
			m_phase = RetryProgress::Failed;
			setStatus( RtspStatus::Error );
		}
	}
	emit autoReconnectChanged();
}

// 最大重连次数
int RtspStreamController::maxReconnect() const {
	return m_inputConfig.maxReconnect;
}

void RtspStreamController::setMaxReconnect( int maxReconnect ) {
	if (m_inputConfig.maxReconnect == maxReconnect)
		return;
	m_inputConfig.maxReconnect = qMax( 0, maxReconnect );
	emit autoReconnectChanged();
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
	emit rtspUrlChanged();
	m_restartTimer.start();
}

// RTSP配置
RtspConfig RtspStreamController::inputConfig() const {
	return m_inputConfig;
}

void RtspStreamController::setInputConfig( const RtspConfig& config ) {
	const bool streamChanged = (config.url != m_inputConfig.url
		|| config.username != m_inputConfig.username
		|| config.password != m_inputConfig.password
		|| config.connectTimeoutMs != m_inputConfig.connectTimeoutMs
		|| config.readTimeoutMs != m_inputConfig.readTimeoutMs);
	const bool policyChanged = (config.autoReconnect != m_inputConfig.autoReconnect
		|| config.maxReconnect != m_inputConfig.maxReconnect);

	m_inputConfig = config;
	m_rtspUrl = config.url;
	emit inputConfigChanged();

	if (policyChanged)
		emit autoReconnectChanged();
	if (streamChanged)	  // 流更改，需要重新启动拉流
		m_restartTimer.start(); 
}

RtspDataContext RtspStreamController::dataContext() const {
	return m_dataContext;
}

LastestFrameStore RtspStreamController::lastestFrameStore() const {
	return m_lastestFrameStore;
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
	if (m_worker)
		m_worker->requestStop();
}
