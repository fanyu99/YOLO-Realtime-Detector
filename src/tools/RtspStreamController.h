#pragma once
#include <QObject>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QSharedPointer>
#include <QThread>
#include <QImage>

#include <memory>

#include "RtspTypes.h"
#include "RtspDecodeWorker.h"

// Rtsp控制器错误：统一使用 RtspTypes.h 中的 RtspOverallError（worker 与 controller 共用一份，不再单独转换）

// 	Rtsp流控制器，用来连接QML和后端的解码器
/*
实现：
1. 开始/停止拉流，暂停和恢复拉流
2. 设置URL地址
3. 发出状态变化
4. 接收视频帧
5. 提供错误信息
6. 提供基本的流的信息

状态/错误统一流转（单一事实来源）：
Worker(解码线程) --statusChanged/errorOccurred--> Controller(缓存为属性) --> QML(只读绑定)
controller 不再自行决定状态，只负责转发指令给 worker、缓存 worker 上报的状态与错误。
*/
class RtspStreamController : public QObject {
	Q_OBJECT

public:
	// 统一状态（与 worker 使用同一份枚举）
	using Status = RtspStatus;

public:

	using ThreadPtr = std::unique_ptr<QThread>;
	using WorkerPtr = std::unique_ptr<RtspDecodeWorker>;
	explicit RtspStreamController( QObject* parent = nullptr );
	explicit RtspStreamController( const RtspConfig& config, QObject* parent = nullptr );
	~RtspStreamController();
	// QML获取门面
	Q_PROPERTY( QUrl rtspUrl READ getUrl WRITE setUrl NOTIFY rtspUrlChanged)
	Q_PROPERTY( RtspStatus currentStatus READ currentStatus NOTIFY StatusChanged )
	Q_PROPERTY( RtspConfig inputConfig READ inputConfig WRITE setInputConfig NOTIFY inputConfigChanged )
	Q_PROPERTY( RtspDataContext dataContext READ dataContext NOTIFY StreamInfoChanged )
	Q_PROPERTY( RtspOverallError currentError READ currentError NOTIFY ErrorOccurred )
	Q_PROPERTY( LastestFrameStore lastestFrameStore READ lastestFrameStore NOTIFY FrameUpdated )

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
	// 开关自动重连
	Q_INVOKABLE void setAutoReconnect( bool autoReconnect );
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
	void StreamInfoChanged( const RtspDataContext& newInfo );
	void Reconnecting();
	void ErrorOccurred( const RtspOverallError& error );
	void rtspUrlChanged();
	void inputConfigChanged();

private:
	// 懒加载创建 worker（未创建线程），并连接 worker -> controller 的转发信号
	void ensureWorkerCreated();
	// worker 上报状态的接收槽：缓存并转发给 QML
	void onWorkerStatusChanged( RtspStatus status );
	// worker 上报错误的接收槽：缓存并转发给 QML
	void onWorkerError( const RtspOverallError& error );

private:
	// m_worker 必须先于 m_thread 声明：
	// 析构顺序与声明顺序相反，releaseResources() 中也是先停线程再销毁 worker。
	WorkerPtr m_worker; // Worker指针
	ThreadPtr m_thread; // 工作线程指针
	QUrl m_rtspUrl{}; // RTSP URL
	RtspConfig m_inputConfig{};	 // 输入配置
	RtspDataContext m_dataContext{}; // 流信息
	RtspStatus m_currentStatus{ RtspStatus::Idle }; // 当前状态（由 worker 信号驱动，QML 只读）
	RtspOverallError m_currentError{}; // 当前错误（由 worker 信号驱动）
	LastestFrameStore m_lastestFrameStore{}; // 最新帧信息,用于QML获取最新帧


};
