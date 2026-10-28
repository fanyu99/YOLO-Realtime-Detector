#pragma once
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

#include "DetectionTypes.h"
#include "DetectionWorker.h"

// 调度层。
//   对上：给 App 提供「提交作业 / 取消 / 查进度」的接口与作业级信号。
//   对下：把作业拆成 Task 投递给 worker，并把 worker 的上报汇总成 job 级事件。
class DetectionManager : public QObject {
	Q_OBJECT
public:
	explicit DetectionManager(QObject* parent = nullptr);
	DetectionManager(const DetectionManager&) = delete;
	DetectionManager& operator=(const DetectionManager&) = delete;

	~DetectionManager() override;

	// 应用配置：内部完成停旧 worker + 用新配置建新 worker
	bool applyConfig(const DetectorConfig& config);

	// 确保 worker 已按当前配置启动。
	bool start();

	void shutdown();								// 停止 worker 并清理全部作业
	const DetectorConfig& config() const { return m_config; }

	quint64 submitSource(FrameSourcePtr source, const QString& saveDir = QString());
	quint64 submitBatch(QVector<FrameSourcePtr> sources, const QString& saveDir = QString());
	quint64 submit(DetectionJob job);				// 通用入口

	bool cancel(quint64 jobId);
	float progress(quint64 jobId) const;			// 0-1；总量未知时返回 0
	bool indeterminate(quint64 jobId) const;		// 总量未知（如摄像头流）
	bool isContained(quint64 jobId) const;	   // 是否包含Job

signals:
	void jobStarted(quint64 jobId);
	void jobProgress(quint64 jobId, float progress);
	void frameReady(quint64 jobId, const DetectionResult& result, const QImage& frame);	// 实时预览：逐单元推送
	// 作业结束（本 Job 的全部 Task 都到达终态）时发一次。
	void jobFinished(quint64 jobId, DetectionJobType type,
					 const QVector<DetectionResult>& results,
					 int totalTasks, int failedTasks);
	void jobFailed(quint64 jobId, const DetectorError& error);
	// 结果保存
	void jobSaved(quint64 jobId, const QString& dir, int fileCount);
	void jobCancelled(quint64 jobId);
	void detectorWarning(const DetectorWarning& warning);	// 检测器警告(非致命)，供 UI 显式提示
	void configAdjusted(const DetectorConfig& config);		// 检测器按模型对齐了配置(如输入尺寸)，供上层同步

private slots:
	void onDetectorWarning(const DetectorWarning& warning);
	void onDetectorFailed(const DetectorError& error);
	void onTaskStarted(quint64 jobId);
	void onTaskProgress(quint64 jobId, quint64 taskId, int unitDone, int unitTotal);
	void onTaskResult(quint64 jobId, const DetectionResult& result, const QImage& frame);
	void onTaskCompleted(quint64 jobId, const QVector<DetectionResult>& results);
	void onTaskFailed(quint64 jobId, const DetectorError& error);
	void onTaskCancelled(quint64 jobId);

private:
	quint64 nextJobId() { return ++m_nextJobId; }
	quint64 nextTaskId() { return ++m_nextTaskId; }
	quint64 dispatch(DetectionJob job);
	// 一个 Task 到达终态后的统一收尾：累加计数，全部结束后才发作业级Job信号
	void settleTask(quint64 jobId, bool failed, bool cancelled,
					const DetectorError& error,
					const QVector<DetectionResult>& results);
	void advanceTaskCounters(quint64 jobId, bool failed);	// 记一个 Task 结束，并广播新进度
	std::unique_ptr<DetectionWorker> m_worker;		// 单路输入先用 1 个 worker
	QHash<quint64, TaskSharedPtr> m_jobs;			// jobId : 共享状态
	QHash<quint64, int> m_pendingTasks;				// jobId : 尚未结束的 Task 数
	QSet<quint64> m_startedJobs;					// 已经发过 jobStarted 的作业(保证一个 Job 只发一次)
	quint64 m_nextJobId = 0;
	quint64 m_nextTaskId = 0;
	DetectorConfig m_config;
};
