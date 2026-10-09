#include "DetectionManager.h"
#include <QMetaType>
#include "Logger.h"

DetectionManager::DetectionManager(QObject* parent) : QObject(parent) {
	// 跨线程的 queued 连接要求参数类型能被元系统识别。
	qRegisterMetaType<DetectionResult>();
	qRegisterMetaType<DetectorError>();
	qRegisterMetaType<DetectorWarning>();
	qRegisterMetaType<QVector<DetectionResult>>();
}

DetectionManager::~DetectionManager() {
	shutdown();
}

// 应用配置  停掉旧 worker + 用新配置建新 worker。
bool DetectionManager::applyConfig(const DetectorConfig& config) {
	if (!m_jobs.isEmpty()) {
		Logger::instance()->warning(QStringLiteral("[DetectionManager]: 有作业进行中，配置未应用"));
		return false;
	}
	if (!config.isValid() || !config.model_exists()) {
		Logger::instance()->error(QStringLiteral("[DetectionManager]: 配置无效或模型不存在，配置未应用"));
		return false;
	}

	m_config = config;
	shutdown();				// 停掉旧 worker（幂等）
	return start();			// 用新配置重建
}

bool DetectionManager::start() {
	// 幂等：已经在跑就直接返回
	if (m_worker && m_worker->isRunning())
		return true;

	if (!m_config.isValid() || !m_config.model_exists()) {
		Logger::instance()->error(QStringLiteral("[DetectionManager]: 配置无效或模型不存在，无法启动检测器"));
		return false;
	}

	// "新建 worker"，所以必然要注入配置。
	m_worker = std::make_unique<DetectionWorker>();
	connect(m_worker.get(), &DetectionWorker::detectorFailed,  this, &DetectionManager::onDetectorFailed);
	connect(m_worker.get(), &DetectionWorker::detectorWarning, this, &DetectionManager::onDetectorWarning);
	connect(m_worker.get(), &DetectionWorker::taskStarted,    this, &DetectionManager::onTaskStarted);
	connect(m_worker.get(), &DetectionWorker::taskProgress,   this, &DetectionManager::onTaskProgress);
	connect(m_worker.get(), &DetectionWorker::taskResult,     this, &DetectionManager::onTaskResult);
	connect(m_worker.get(), &DetectionWorker::taskCompleted,  this, &DetectionManager::onTaskCompleted);
	connect(m_worker.get(), &DetectionWorker::taskFailed,     this, &DetectionManager::onTaskFailed);
	connect(m_worker.get(), &DetectionWorker::taskCancelled,  this, &DetectionManager::onTaskCancelled);

	m_worker->setConfig(m_config);
	return m_worker->start();
}

void DetectionManager::shutdown() {
	if (m_worker) {
		// stop() 会把队列里未开始的 Task 以 taskCancelled信号上报，
		m_worker->stop();
		m_worker.reset();
	}
	m_jobs.clear();
	m_pendingTasks.clear();
}

// 提交作业

quint64 DetectionManager::submitSource(FrameSourcePtr source, const QString& saveDir) {
	if (!source) {
		emit jobFailed(0, DetectorError(QStringLiteral("[DetectionManager]: 帧源为空!"),
										DetectorErrorCategory::Other, DetectorErrorCode::UnknownError));
		return 0;
	}
	return submit(makeSingleTaskJob(nextJobId(), nextTaskId(), std::move(source), saveDir));
}

quint64 DetectionManager::submitBatch(QVector<FrameSourcePtr> sources, const QString& saveDir) {
	if (sources.isEmpty()) {
		emit jobFailed(0, DetectorError(QStringLiteral("[DetectionManager]: 批量输入为空!"),
										DetectorErrorCategory::Image, DetectorErrorCode::InvalidImage));
		return 0;
	}
	return submit(makeBatchJob(nextJobId(), std::move(sources), saveDir));
}

quint64 DetectionManager::submit(DetectionJob job) {
	if (job.jobId == 0)
		job.jobId = nextJobId();
	if (!job.shared)
		job.shared = std::make_shared<TaskShared>(job.jobId, job.type);

	if (job.tasks.empty()) {
		emit jobFailed(job.jobId, DetectorError(QStringLiteral("[DetectionManager]: 作业不含任何任务!"),
												DetectorErrorCategory::Other, DetectorErrorCode::UnknownError));
		return 0;
	}

	// 补全每个 Task 的归属信息（批量作业由调用方建好 Task 列表，这里统一收口）
	for (auto& task : job.tasks) {
		if (task->taskId == 0)
			task->taskId = nextTaskId();
		task->jobId = job.jobId;
		task->shared = job.shared;
	}

	return dispatch(std::move(job));
}

quint64 DetectionManager::dispatch(DetectionJob job) {
	if (!m_worker || !m_worker->isRunning()) {
		emit jobFailed(job.jobId, DetectorError(QStringLiteral("[DetectionManager]: 工作线程未启动!"),
												DetectorErrorCategory::Other, DetectorErrorCode::UnknownError));
		return 0;
	}

	const int total = static_cast<int>(job.tasks.size());

	job.shared->totalTasks.store(total, std::memory_order_relaxed);

	m_jobs.insert(job.jobId, job.shared);
	m_pendingTasks.insert(job.jobId, total);

	int accepted = 0;
	for (auto& task : job.tasks) {
		if (m_worker->submit(std::move(task)))
			++accepted;
	}

	if (accepted == 0) {
		m_jobs.remove(job.jobId);
		m_pendingTasks.remove(job.jobId);
		emit jobFailed(job.jobId, DetectorError(QStringLiteral("[DetectionManager]: 任务投递失败!"),
												DetectorErrorCategory::Other, DetectorErrorCode::UnknownError));
		return 0;
	}
	if (accepted < total)
		Logger::instance()->warning(QStringLiteral("[DetectionManager]: 部分任务投递失败，作业 ")
			+ QString::number(job.jobId));

	Logger::instance()->info(QStringLiteral("[DetectionManager]: 作业 ") + QString::number(job.jobId)
		+ QStringLiteral(" 已提交，任务数: ") + QString::number(accepted));
	return job.jobId;
}

// 查询 / 取消

bool DetectionManager::cancel(quint64 jobId) {
	const TaskSharedPtr shared = m_jobs.value(jobId);
	if (!shared)
		return false;

	// 只是置一个原子标志：worker 在每个工作单元边界检查它
	shared->requestCancel();
	Logger::instance()->info(QStringLiteral("[DetectionManager]: 请求取消作业 ") + QString::number(jobId));
	return true;
}

float DetectionManager::progress(quint64 jobId) const {
	const TaskSharedPtr shared = m_jobs.value(jobId);
	return shared ? shared->progress() : 0.0f;
}

bool DetectionManager::indeterminate(quint64 jobId) const {
	const TaskSharedPtr shared = m_jobs.value(jobId);
	return shared ? shared->indeterminate() : false;
}

bool DetectionManager::isContained(quint64 jobId) const {
	return m_jobs.contains(jobId);
}

// worker 上报 -> 作业级事件

// 检测器警告(非致命)
void DetectionManager::onDetectorWarning(const DetectorWarning& warning) {
	if (warning.code == DetectorWarning::Code::InputSizeAligned) {
		m_config.input_width = warning.to_width;
		m_config.input_height = warning.to_height;
		emit configAdjusted(m_config);
	}
	emit detectorWarning(warning);
}

void DetectionManager::onDetectorFailed(const DetectorError& error) {
	Logger::instance()->error(QStringLiteral("[DetectionManager]: 检测器初始化失败: ") + error.errorMessage);
}

void DetectionManager::onTaskStarted(quint64 jobId) {
	if (m_startedJobs.contains(jobId))
		return;
	m_startedJobs.insert(jobId);
	emit jobStarted(jobId);
}

void DetectionManager::onTaskProgress(quint64 jobId, quint64 taskId, int unitDone, int unitTotal) {
	Q_UNUSED(taskId);
	Q_UNUSED(unitDone);
	Q_UNUSED(unitTotal);
	// 进度以共享状态为准：它同时含 Job 级的 task 计数与当前 Task 的单元进度，
	const TaskSharedPtr shared = m_jobs.value(jobId);
	if (shared)
		emit jobProgress(jobId, shared->progress());
}

void DetectionManager::onTaskResult(quint64 jobId, const DetectionResult& result, const QImage& frame) {
	emit frameReady(jobId, result, frame);
}

// 记一个 Task 结束：累加 Job 级计数、清掉单元进度、广播新进度。
// 取消不算"完成"，所以不在 cancel 路径上调用（否则进度会跳到 100%）。
void DetectionManager::advanceTaskCounters(quint64 jobId, bool failed) {
	const TaskSharedPtr shared = m_jobs.value(jobId);
	if (!shared)
		return;

	shared->completedTasks.fetch_add(1, std::memory_order_relaxed);
	if (failed)
		shared->failedTasks.fetch_add(1, std::memory_order_relaxed);
	shared->resetCurrentUnit();			// 当前 Task 结束，单元比例归零

	emit jobProgress(jobId, shared->progress());
}

void DetectionManager::onTaskCompleted(quint64 jobId, const QVector<DetectionResult>& results) {
	settleTask(jobId, false, false, DetectorError{}, results);
}

void DetectionManager::onTaskFailed(quint64 jobId, const DetectorError& error) {
	// 单个 Task 失败不再直接宣告整个作业失败：批量作业里一个坏文件
	// 不该让剩下的几十个文件全部作废。失败先计数，等作业收尾时再判断。
	Logger::instance()->warning(QStringLiteral("[DetectionManager]: 作业 ") + QString::number(jobId)
		+ QStringLiteral(" 中有一个任务失败: ") + error.errorMessage);
	settleTask(jobId, true, false, error, {});
}

void DetectionManager::onTaskCancelled(quint64 jobId) {
	settleTask(jobId, false, true, DetectorError{}, {});
}

// 一个 Task 到达终态后的统一收尾。
void DetectionManager::settleTask(quint64 jobId, bool failed, bool cancelled,
								  const DetectorError& error,
								  const QVector<DetectionResult>& results)
{
	if (!cancelled) {
		advanceTaskCounters(jobId, failed);
	}

	const int left = m_pendingTasks.value(jobId, 1) - 1;
	if (left > 0) {
		m_pendingTasks.insert(jobId, left);
		return;								// 还有 Task 没结束，继续等
	}
	m_pendingTasks.remove(jobId);
	m_startedJobs.remove(jobId);		// 作业收尾，允许同 id 复用（也防止集合无限增长）

	const TaskSharedPtr shared = m_jobs.value(jobId);
	const int total = shared ? shared->totalTasks.load(std::memory_order_relaxed) : 0;
	const int failedCount = shared ? shared->failedTasks.load(std::memory_order_relaxed) : 0;
	const DetectionJobType type = shared ? shared->jobType : DetectionJobType::SingleImage;
	const int saved = shared ? shared->savedFiles.load(std::memory_order_relaxed) : 0;
	const QString saveDir = shared ? shared->saveDir : QString();
	m_jobs.remove(jobId);

	// 保存结果先报：App 的横幅靠它，而且它在终态里会清掉 currentJobId
	if (saved > 0 && !saveDir.isEmpty())
		emit jobSaved(jobId, saveDir, saved);

	if (cancelled)
		emit jobCancelled(jobId);
	else if (total > 0 && failedCount >= total)
		emit jobFailed(jobId, error);		// 全部失败才算作业失败
	else
		emit jobFinished(jobId, type, results, total, failedCount);
}
