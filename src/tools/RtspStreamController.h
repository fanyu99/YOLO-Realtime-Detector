#pragma once
#include <QObject>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QSharedPointer>
#include <QThread>
#include <QImage>
#include <QTimer>

#include <memory>

#include "RtspTypes.h"
#include "RtspDecodeWorker.h"


// 	Rtsp流控制器，用来连接QML和后端的解码器
/*
实现：
1. 开始/停止拉流，暂停和恢复拉流
2. 设置URL地址
3. 发出状态变化
4. 接收视频帧
5. 提供错误信息
6. 提供基本的流的信息

状态/错误统一流转：
Worker(解码线程) --statusChanged/errorOccurred/finished--> Controller(缓存错误/状态供QML读取) --> QML(只读绑定)

重连策略由Controller进行管理：
重连需要进行退避操作，防止短时间频繁发起重连；
	Playing/Opening --error(可重试)--> Reconnecting --退避延时--> Opening -> ... 重复
	-- >不可重试 / 超限--> Error（终态）+ ReconnectFailed 一次性通知 QML

配置防抖策略：
需要间隔一定时间才会应用新的流配置进行重启
	
	*/
class RtspStreamController : public QObject {
	Q_OBJECT

public:
	using Status = RtspStatus;

public:

	using ThreadPtr = std::unique_ptr<QThread>;
	using WorkerPtr = std::unique_ptr<RtspDecodeWorker>;
	explicit RtspStreamController( QObject* parent = nullptr );
	explicit RtspStreamController( const RtspConfig& config, QObject* parent = nullptr );
	~RtspStreamController();
	// QML获取门面
	Q_PROPERTY( QUrl rtspUrl READ getUrl WRITE setUrl NOTIFY rtspUrlChanged )
		Q_PROPERTY( RtspStatus currentStatus READ currentStatus NOTIFY StatusChanged )
		Q_PROPERTY( RtspConfig inputConfig READ inputConfig WRITE setInputConfig NOTIFY inputConfigChanged )
		Q_PROPERTY( RtspDataContext dataContext READ dataContext NOTIFY DataContextChanged )
		Q_PROPERTY( RtspOverallError currentError READ currentError NOTIFY ErrorOccurred )
		Q_PROPERTY( LastestFrameStore lastestFrameStore READ lastestFrameStore NOTIFY FrameUpdated )
		Q_PROPERTY( bool autoReconnect READ autoReconnect WRITE setAutoReconnect NOTIFY autoReconnectChanged )
		Q_PROPERTY( int maxReconnect READ maxReconnect WRITE setMaxReconnect NOTIFY autoReconnectChanged )
		Q_PROPERTY( int retryCount READ retryCount NOTIFY retryCountChanged )
		Q_PROPERTY( RtspStreamStatus streamStatus READ streamStatus NOTIFY StreamStatusChanged )

		// 开始拉流
		Q_INVOKABLE bool start();
	// 停止拉流
	Q_INVOKABLE void stop();
	// 暂停/恢复拉流
	Q_INVOKABLE void pause();
	Q_INVOKABLE	bool restart();
	// 获取当前的状态
	RtspStatus currentStatus() const;
	// 获取当前的错误
	RtspOverallError currentError() const;
	// 是否正在拉流
	bool isStreaming() const;
	// 开关自动重连（只影响重连策略，不重启流）
	bool autoReconnect() const;
	Q_INVOKABLE void setAutoReconnect( bool autoReconnect );
	// 最大重连次数（只影响重连策略，不重启流）
	int maxReconnect() const;
	// 当前的重连次数
	int retryCount() const;
	// 当前的拉流统计信息
	RtspStreamStatus streamStatus() const;
	Q_INVOKABLE void setMaxReconnect( int maxReconnect );
	// 获取/设置URL地址
	QUrl getUrl()const;
	Q_INVOKABLE void setUrl( const QUrl& url );
	// 获取/设置输入配置
	RtspConfig inputConfig() const;
	void setInputConfig( const RtspConfig& config );
	// 获取流信息
	RtspDataContext dataContext() const;
	// 获取最新帧的相关信息
	LastestFrameStore lastestFrameStore() const;
	// 回收所有资源
	void releaseResources();

signals:
	void StatusChanged( RtspStatus status );  // 状态改变，QML进行更新UI
	void FrameUpdated( const QImage& newFrame );// 新帧可用
	void StreamOpened();
	void StreamClosed();
	void DataContextChanged( const RtspDataContext& newInfo );
	void Reconnecting();  // 进入了一次重连等待
	void ReconnectAttempt( int attempt, int maxAttempts ); // 第 attempt 次重连
	void ReconnectFailed( int attempts, const RtspOverallError& lastError ); // 重连彻底失败，终态
	void ErrorOccurred( const RtspOverallError& error );
	void rtspUrlChanged();
	void inputConfigChanged();
	void autoReconnectChanged();
	void retryCountChanged();
	void StreamStatusChanged();


private:
	// 当前的重连执行状态
	enum class RetryProgress {
		Idle,          // 未拉流
		Streaming,     // worker 正在跑
		WaitingRetry,  // 等待定时器,重启
		Failed         // 重连彻底失败（终态）
	};

	// 懒加载创建 worker 与线程（只创建一次）
	void ensureWorkerCreated();
	// 启动一次解码任务（重置计数、投递 startDecoding 到解码线程）
	void launchWorker();
	// 安排一次带退避的重连
	void scheduleRetry();
	// 放弃重连，进入终态并通知 QML
	void giveUp();
	// 配置变更间隔结束，进行重启：流参数(URL/配置等)变了才重启
	void onRestartTimeout();
	// 统一写状态并发通知
	void setStatus( RtspStatus status );
	// 判定错误是否可重连
	static bool isRetryable( RtspOverallError::ErrorCode code );
	// 第 attempt 次的规避时长ms（逐步增加间隔时长），防止频繁重连
	static int backoffMs( int attempt );

	// worker 上报状态的接收槽：缓存并转发给 QML
	void onWorkerStatusChanged( RtspStatus status );
	// worker 上报错误的接收槽：缓存并转发给 QML
	void onWorkerError( const RtspOverallError& error );
	// worker 结束
	// clean = true  : 正常 EOF 或被主动停止
	// clean = false : 因错误结束，error 为最后一次错误
	void onWorkerFinished( bool clean, const RtspOverallError& error );
	// 重连定时器到期
	void onRetryTimeout();

private:
	// m_worker 必须先于 m_thread 声明：
	// 析构顺序与声明顺序相反，releaseResources() 中也是先停线程再销毁 worker。
	WorkerPtr m_worker; // Worker指针
	ThreadPtr m_thread; // 工作线程指针
	QUrl m_rtspUrl{}; // RTSP URL
	RtspConfig m_inputConfig{};	 // 输入配置（controller 持有唯一副本，启动时投递给 worker）
	RtspDataContext m_dataContext{}; // 流信息
	RtspStatus m_currentStatus{ RtspStatus::Idle }; // 当前状态（controller 统一写入，QML 只读）
	RtspOverallError m_currentError{}; // 当前错误
	LastestFrameStore m_lastestFrameStore{}; // 最新帧信息,用于QML获取最新帧
	RtspStreamStatus m_streamStatus{}; // 拉流统计信息,用于QML获取

	RetryProgress m_phase{ RetryProgress::Idle }; // 控制器重连状态机
	int m_retryCount{ 0 }; // 当前已尝试重连次数
	bool m_pendingRestart{ false }; // 如果正在working，配置变更，等 worker 退出再重启
	QTimer m_retryTimer; // 单发：退避后重启
	QTimer m_restartTimer; // 单发：配置变更防抖
};
