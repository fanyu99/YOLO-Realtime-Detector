#include "RtspStreamController.h"

RtspStreamController::RtspStreamController( QObject* parent )
	: QObject( parent )
{
	// 跨线程队列连接前注册元类型
	qRegisterMetaType<RtspStatus>( "RtspStatus" );
	qRegisterMetaType<RtspOverallError>( "RtspOverallError" );
	qRegisterMetaType<LastestFrameStore>( "LastestFrameStore" );
	qRegisterMetaType<RtspConfig>( "RtspConfig" );
	qRegisterMetaType<RtspDataContext>( "RtspDataContext" );
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

// 懒加载创建 worker，并把 worker 的信号转发到 controller 的缓存槽
void RtspStreamController::ensureWorkerCreated() {
	if (m_worker)
		return;
	m_worker = std::make_unique<RtspDecodeWorker>( m_inputConfig );

	// 状态转发：Worker(解码线程) -> Controller(缓存) -> QML(绑定)
	connect( m_worker.get(), &RtspDecodeWorker::statusChanged,
		this, &RtspStreamController::onWorkerStatusChanged );
	// 错误转发
	connect( m_worker.get(), &RtspDecodeWorker::errorOccurred,
		this, &RtspStreamController::onWorkerError );
	// 帧转发
	connect( m_worker.get(), &RtspDecodeWorker::frameDecoded, this, [ this ]( const LastestFrameStore& newFrame ) {
		m_lastestFrameStore = newFrame;
		emit FrameUpdated( newFrame.lastestFrame );
		} );
	// 流正常结束（EOF）时也视为已停止
	connect( m_worker.get(), &RtspDecodeWorker::streamFinished, this, [ this ] {
		emit StreamClosed();
		} );
}

// worker 上报状态的接收槽：只缓存 + 转发，状态机由 worker 驱动
void RtspStreamController::onWorkerStatusChanged( RtspStatus status ) {
	if (m_currentStatus == status)
		return;
	m_currentStatus = status;
	emit StatusChanged( status );

	switch (status) {
	case RtspStatus::Opened:
		emit StreamOpened();
		break;
	case RtspStatus::Reconnecting:
		emit Reconnecting();
		break;
	case RtspStatus::Stopped:
	case RtspStatus::Idle:
		emit StreamClosed();
		break;
	default:
		break;
	}
}

// worker 上报错误的接收槽：只缓存 + 转发
void RtspStreamController::onWorkerError( const RtspOverallError& error ) {
	m_currentError = error;
	emit ErrorOccurred( error );
}

// 开始拉流
bool RtspStreamController::start() {
	if (isStreaming()) {
		return true; // 已经在拉流中
	}
	if (!m_inputConfig.isValid()) {
		onWorkerError( RtspOverallError::InvalidConfigError( QStringLiteral( "无效配置，无法开始拉流" ) ) );
		return false;
	}

	// 开始新的拉流前清空上一次的错误
	m_currentError = RtspOverallError{};

	ensureWorkerCreated();
	m_worker->setRtspConfig( m_inputConfig );

	// 已存在 worker（上次已停止）：复用同一个线程，排队重新启动解码
	if (m_thread) {
		QMetaObject::invokeMethod( m_worker.get(), &RtspDecodeWorker::startDecoding, Qt::QueuedConnection );
		return true;
	}

	// 首次启动：创建工作线程并将worker移动到线程中
	m_thread = std::make_unique<QThread>();
	m_worker->moveToThread( m_thread.get() );
	// 线程启动后在解码线程里执行 startDecoding()
	connect( m_thread.get(), &QThread::started, m_worker.get(), &RtspDecodeWorker::startDecoding );
	// 解码循环退出后让线程事件循环也退出，便于下次复用
	connect( m_worker.get(), &RtspDecodeWorker::streamFinished, m_thread.get(), &QThread::quit );

	m_thread->start();
	return true;
}

// 停止拉流
void RtspStreamController::stop() {
	if (m_worker && m_worker->isRunning()) {
		m_worker->requestStop(); // 由于跨线程，需要使用原子变量进行线程安全地停止请求
	}
	// 状态不在这里直接改：等 worker 退出解码循环后上报 Stopped，再由 onWorkerStatusChanged 更新
}

// 回收所有资源
void RtspStreamController::releaseResources() {
	if (m_worker) {
		m_worker->requestStop();
	}
	if (m_thread) {
		m_thread->quit();
		m_thread->wait();
	}
	// 先停线程，再销毁 worker，避免 worker 在已退出的线程里被使用
	m_worker.reset();
	m_thread.reset();
}

// 暂停拉流
void RtspStreamController::pause() {
	if (m_worker && m_worker->isRunning()) {
		m_worker->pauseDecoding();
	}
}

// 恢复拉流
bool RtspStreamController::restart() {
	if (m_worker && m_currentStatus == RtspStatus::Paused) {
		m_worker->resumeDecoding();
		return true;
	}
	onWorkerError( RtspOverallError{ QStringLiteral( "恢复拉流失败！" ), RtspOverallError::Other } );
	return false;
}

// 是否正在拉流
bool RtspStreamController::isStreaming()const {
	return m_worker && m_worker->isRunning();
}

// 设置自动重连
void RtspStreamController::setAutoReconnect( bool autoReconnect ) {
	m_inputConfig.autoReconnect = autoReconnect;
	if (m_worker) {
		m_worker->setRtspConfig( m_inputConfig );
	}
}

// 获取当前的状态
RtspStatus RtspStreamController::currentStatus() const {
	return m_currentStatus;
}

// 获取当前的错误
RtspOverallError RtspStreamController::currentError() const {
	return m_currentError;
}

// 获取/设置URL地址
QUrl RtspStreamController::getUrl() const {
	return m_rtspUrl;
}

void RtspStreamController::setUrl( const QUrl& url ) {
	if (m_rtspUrl == url)
		return;
	m_rtspUrl = url;
	m_inputConfig.url = url;
	if (m_worker) {
		m_worker->setRtspUrl( url.toString() );
	}
	emit rtspUrlChanged();
}

// 获取/设置输入配置
RtspConfig RtspStreamController::inputConfig() const {
	return m_inputConfig;
}

void RtspStreamController::setInputConfig( const RtspConfig& config ) {
	m_inputConfig = config;
	m_rtspUrl = config.url;
	if (m_worker) {
		m_worker->setRtspConfig( config );
	}
	emit inputConfigChanged();
}

// 获取流信息
RtspDataContext RtspStreamController::dataContext() const {
	return m_dataContext;
}

// 获取最新帧的相关信息
LastestFrameStore RtspStreamController::lastestFrameStore() const {
	return m_lastestFrameStore;
}
