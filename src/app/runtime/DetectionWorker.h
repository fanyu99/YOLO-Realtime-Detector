#pragma once
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>

#include <QImage>
#include <QObject>
#include <QThread>

#include "DetectionTypes.h"
#include "FrameSource.h"
#include "DetectionSession.h"

class DetectionResultSaver;

// Worker 负责执行DetectionTask
// 执行单元：在一条独立线程里跑 work() 循环，从队列取 Task 执行。
class DetectionWorker : public QObject {
	Q_OBJECT
public:
	explicit DetectionWorker(QObject* parent = nullptr);
	~DetectionWorker() override;

	// 设置检测器配置。必须在 start() 之前调用：
	void setConfig(const DetectorConfig& config);

	bool start();					// 启动工作线程；失败返回 false
	void stop();					// 停止工作线程，并丢弃队列中尚未开始的 Task
	bool isRunning() const { return m_running.load(std::memory_order_relaxed); }

	// 投递任务（由 Manager 调用，线程安全）。
	// 返回 false 表示未启动或任务为空，调用方据此上报失败。
	bool submit(std::unique_ptr<DetectionTask> task);

signals:

	void fallbackOccurred(const DetectionError& error); // 触发fallback并完成自愈，通知上层进行更改配置以及警告

	void started();												// 工作线程已就绪
	void stopped();												// 工作线程已退出
	void detectorFailed(const DetectionError& error);			// 模型加载失败（在 worker 线程内）
	void detectorWarning(const DetectorWarning& warning);		// 检测器警告（非致命，如输入尺寸自动对齐）

	void taskStarted(quint64 jobId);	 // 任务开始执行
	void taskProgress(quint64 jobId, quint64 taskId, int unitDone, int unitTotal);	// 向上Manager汇报当前 Task 内部的工作单元进度
	void taskResult(quint64 jobId, const DetectionResult& result, const QImage& frame);	// 每个工作单元的即时结果
	void taskCompleted(quint64 jobId, const QVector<DetectionResult>& results);
	void taskFailed(quint64 jobId, const DetectionError& error);
	void taskCancelled(quint64 jobId);

private:
	// 转发警告并修正尺寸
	void reportWarnings();

	// 重建会话,fallback机制，其中发送fallbackOccurred信号并记录日志并发送警告信息
	// 返回是否成功
	bool rebuildSession();

	DetectionSession* createSession();	// 在 worker 线程内创建和管理会话
	void work();					// 工作循环
	void execute(DetectionTask& task);  // 总入口
	bool runImage(DetectionTask& task);
	bool runStream(DetectionTask& task);
	void finishUnit(DetectionTask& task, const cv::Mat& frame, DetectionResult& result,DetectionResultSaver& saver);	// 完成一个task,进行总结
	void reportSaveResult(const DetectionResultSaver& saver);	// Task 结束后上报一次保存结果
	void failTaskIfUnfinished(DetectionTask& task);		// 任务抛异常时补发失败信号(避免重复发)

	std::unique_ptr<QThread> m_thread;		// 工作线程
	std::unique_ptr<DetectionSession> m_session;	// 检测会话（持有推理流程与帧源工厂），由Worker管理生命周期
	DetectorConfig m_config;					   // 保存内部的Detector配置
	DetectionError m_lastError;					// 只在 worker 线程内写

	std::queue<std::unique_ptr<DetectionTask>> m_queue;
	mutable std::mutex m_mutex;					// 保护 m_queue
	std::condition_variable m_cv;
	std::atomic<bool> m_running{ false };
};
