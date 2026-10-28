#include "DetectionWorker.h"
#include "DetectionRenderer.h"
#include "DetectionResultSaver.h"
#include <vector>
#include <QElapsedTimer>
#include <opencv2/opencv.hpp>
#include "Logger.h"
#include <span>

DetectionWorker::DetectionWorker(QObject* parent) : QObject(parent) {}

DetectionWorker::~DetectionWorker() {
	stop();
}

// 设置配置
void DetectionWorker::setConfig(const DetectorConfig& config) {
	if (m_running.load(std::memory_order_relaxed)) {
		Logger::instance()->warning(QStringLiteral("[DetectionWorker]: 工作线程运行中，忽略配置变更"));
		return;
	}
	m_config = config;
}

// 启动工作线程
bool DetectionWorker::start() {
	if (m_running.load(std::memory_order_relaxed))
		return true;							// 已在运行

	if (m_config.model_path.empty()) {
		Logger::instance()->error(QStringLiteral("[DetectionWorker]: 未设置检测器配置，无法启动"));
		return false;
	}

	m_running.store(true, std::memory_order_relaxed);

	m_thread = std::unique_ptr<QThread>(QThread::create([ this ]() {
		try {
			work();
		}
		catch (const std::exception& e) {
			Logger::instance()->error(QStringLiteral("[DetectionWorker]: 工作线程异常退出: ")
				+ QString::fromUtf8(e.what()));
			emit detectorFailed(DetectorError(
				QStringLiteral("[DetectionWorker]: 工作线程异常: ") + QString::fromUtf8(e.what()),
				DetectorErrorCategory::Other, DetectorErrorCode::UnknownError));
		}
		catch (...) {
			Logger::instance()->error(QStringLiteral("[DetectionWorker]: 工作线程发生未知异常"));
			emit detectorFailed(DetectorError(
				QStringLiteral("[DetectionWorker]: 工作线程发生未知异常"),
				DetectorErrorCategory::Other, DetectorErrorCode::UnknownError));
		}
		// 线程结束: 复位状态
		m_running.store(false, std::memory_order_relaxed);
	}));
	// 启动线程
	if (m_thread)
		m_thread->start();

	Logger::instance()->info(QStringLiteral("[DetectionWorker]: 工作线程已启动"));
	return true;
}

// 停止工作线程,会丢弃队列中还没有开始的task
void DetectionWorker::stop() {
	if (!m_thread)
		return;

	std::vector<quint64> dropped;	 // 丢弃的JobId
	m_running.store(false, std::memory_order_relaxed);
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		// 丢弃尚未开始的 Task,让 Manager 知道
		while (!m_queue.empty()) {
			dropped.push_back(m_queue.front()->jobId);
			m_queue.pop();
		}
	}
	m_cv.notify_all();							

	for (quint64 jobId : dropped)		// 信号在锁外发		
		emit taskCancelled(jobId);

	// 释放内存
	m_thread->wait();						
	m_thread.reset();							
	m_detector.reset(); 						

	Logger::instance()->info(QStringLiteral("[DetectionWorker]: 工作线程已停止"));
	emit stopped();
}

// 提交任务
bool DetectionWorker::submit(std::unique_ptr<DetectionTask> task) {
	if (!task)
		return false;

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_running.load(std::memory_order_relaxed))
			return false;
		m_queue.push(std::move(task));
	}
	m_cv.notify_one();	  // 通知一个工作线程
	return true;
}

// 工作循环
void DetectionWorker::work() {
	// 在 worker 线程内加载模型：ONNX 会话构造较重,在worker线程进行构造
	m_detector = std::make_unique<Detector>(m_config);
	if (!m_detector->isValid()) {
		Logger::instance()->error(QStringLiteral("[DetectionWorker]: 检测器初始化失败: ")
			+ m_detector->lastError().errorMessage);
		emit detectorFailed(m_detector->lastError());
	}
	else {
		// 构造期产生的警告(如输入尺寸已按模型自动对齐)必须显式上抛：
		for (const DetectorWarning& warning : m_detector->warnings()) {
			Logger::instance()->warning(warning.message);
			emit detectorWarning(warning);
		}
		Logger::instance()->info(QStringLiteral("[DetectionWorker]: 检测器就绪，运行设备: ")
			+ QString::fromUtf8(m_detector->device()));

		// 预热
		m_detector->warmup(3);
	}
	emit started();
	 // 工作循环
	while(true) {
		std::unique_ptr<DetectionTask> task;
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			m_cv.wait(lock, [ this ]() {
				// 任务队列不为空或者停止时不阻塞
				return !m_running.load(std::memory_order_relaxed) || !m_queue.empty();
				});
			// 如果停止,直接退出
			if (!m_running.load(std::memory_order_relaxed))
				break;					
			// 取任务
			task = std::move(m_queue.front());
			m_queue.pop();
		}
		// 锁外执行耗时任务
		try {
			this->execute(*task);
		}
		catch (const std::exception& e) {
			Logger::instance()->error(QStringLiteral("[DetectionWorker]: 任务执行异常: ")
				+ QString::fromUtf8(e.what()));
			m_lastError = DetectorError(
				QStringLiteral("[DetectionWorker]: 任务执行异常: ") + QString::fromUtf8(e.what()),
				DetectorErrorCategory::Other, DetectorErrorCode::UnknownError);
			failTaskIfUnfinished(*task);
		}
		catch (...) {
			Logger::instance()->error(QStringLiteral("[DetectionWorker]: 任务执行发生未知异常"));
			m_lastError = DetectorError(
				QStringLiteral("[DetectionWorker]: 任务执行发生未知异常"),
				DetectorErrorCategory::Other, DetectorErrorCode::UnknownError);
			failTaskIfUnfinished(*task);
		}
	}
}

// 补发失败信号：只在任务还没落到终态时才
void DetectionWorker::failTaskIfUnfinished(DetectionTask& task) {
	const TaskSharedPtr shared = task.shared;
	if (!shared)
		return;
	const DetectionTaskStatus st = shared->status.load(std::memory_order_relaxed);
	if (st == DetectionTaskStatus::Completed || st == DetectionTaskStatus::Failed
		|| st == DetectionTaskStatus::Cancelled)
		return;
	// 失败计数由 Manager 在收到 taskFailed 时累加，这里不再自己加
	shared->status.store(DetectionTaskStatus::Failed, std::memory_order_relaxed);
	emit taskFailed(task.jobId, m_lastError);
}

// 执行Task
void DetectionWorker::execute(DetectionTask& task) {
	const TaskSharedPtr shared = task.shared; // 获取共享信息
	if (!shared)
		return;
	 // 如果Job取消,emit取消信号
	if (shared->cancelled()) {
		emit taskCancelled(task.jobId);
		return;
	}

	shared->status.store(DetectionTaskStatus::Running, std::memory_order_relaxed);
	shared->resetCurrentUnit();			// 清掉上一个 Task 留下的单元进度
	task.fps.reset();					// 帧率从初始值重新起算，不继承上一个 Task
	emit taskStarted(task.jobId);

	bool ok = false;	// 执行是否成功
	if (!m_detector || !m_detector->isValid()) {
		m_lastError = m_detector   // 获取检测器的最后错误信息
			? m_detector->lastError()
			: DetectorError(QStringLiteral("[DetectionWorker]: 检测器未初始化!"),
				DetectorErrorCategory::Other, DetectorErrorCode::UnknownError);
	}
	else if (!task.source) {
		m_lastError = DetectorError(QStringLiteral("[DetectionWorker]: 帧源为空!"),
			DetectorErrorCategory::Other, DetectorErrorCode::UnknownError);
	}
	// 打开帧源
	else if (!task.source->open()) {
		// 打开失败的错误分类沿用旧实现：图片归 Image，视频/摄像头归 Detection
		const bool isImage = (task.source->type() == FrameSourceType::SingleImage);
		m_lastError = DetectorError(QStringLiteral("[DetectionWorker]: ") + task.source->lastError(),
			isImage ? DetectorErrorCategory::Image : DetectorErrorCategory::Detection,
			isImage ? DetectorErrorCode::InvalidImage : DetectorErrorCode::DetectionFailed);
	}
	else {
		switch (task.source->type()) {
		case FrameSourceType::SingleImage:	ok = runImage(task);  break;
		case FrameSourceType::VideoFile:
		case FrameSourceType::Camera:		ok = runStream(task); break;
		}
		// 显式地进行关闭帧源
		task.source->close();
	}

	// 执行结束,更新共享状态并emit信号
	if (shared->cancelled()) {
		shared->status.store(DetectionTaskStatus::Cancelled, std::memory_order_relaxed);
		emit taskCancelled(task.jobId);
	}
	else if (!ok) {		 // 执行失败（失败计数由 Manager 在收到 taskFailed 时累加）
		shared->status.store(DetectionTaskStatus::Failed, std::memory_order_relaxed);
		emit taskFailed(task.jobId, m_lastError);
	}
	else {
		shared->status.store(DetectionTaskStatus::Completed, std::memory_order_relaxed);
		emit taskCompleted(task.jobId, task.results);
	}
}

// 单张图像
bool DetectionWorker::runImage(DetectionTask& task) {
	task.shared->currentUnitTotal.store(1, std::memory_order_relaxed);

	// 图已在 FrameSource::open() 里读好
	// 直接获取单张图片
	const cv::Mat frame = task.source->getFrame();
	DetectionResult result = m_detector->detect(frame);
	if (result.error.has_error()) {
		m_lastError = result.error;
		return false;
	}
	result.taskId = task.taskId;
	result.source = task.source->displayName();	  // 设置展示名称,例如文件路径/摄像头ID等

	// 保存（未开启保存时 saver 全程 no-op）
	DetectionResultSaver saver;
	const cv::Size size = task.source->frameSize();
	if (!saver.begin(task, size.width, size.height, task.source->fps()) && !saver.lastError().isEmpty())
		Logger::instance()->warning(QStringLiteral("[DetectionWorker]: ") + saver.lastError());
	finishUnit(task, frame, result, saver);
	reportSaveResult(saver);
	return true;
}

// 视频文件 / 摄像头流：逐帧推进
bool DetectionWorker::runStream(DetectionTask& task) {
	FrameSource& source = *task.source;

	// 预读总帧数：摄像头返回 0，代表总量未知（UI 显示忙碌）
	task.shared->currentUnitTotal.store(static_cast<int>(source.totalFrames()), std::memory_order_relaxed);

	DetectionResultSaver saver;
	const cv::Size size = source.frameSize();
	if (!saver.begin(task, size.width, size.height, source.fps()) && !saver.lastError().isEmpty())
		Logger::instance()->warning(QStringLiteral("[DetectionWorker]: ") + saver.lastError());

	cv::Mat frame;
	int processed = 0;
	QElapsedTimer clock;
	clock.start();

	// 读失败/流结束都返回空 Mat，循环退出
	while (!(frame = source.getFrame()).empty()) {
		// 逐帧检查取消
		if (task.shared->cancelled())
			break;								// 取消按正常结束处理，由 execute() 发 taskCancelled

		DetectionResult result = m_detector->detect(frame);
		if (result.error.has_error()) {
			m_lastError = result.error;
			return false;
		}
		result.taskId = task.taskId;
		result.source = source.displayName();
		finishUnit(task, frame, result, saver);
		++processed;
	}

	reportSaveResult(saver);

	const qint64 elapsedMs = clock.elapsed();
	if (processed > 0 && elapsedMs > 0) {
		const double fps = static_cast<double>(processed) * 1000.0 / static_cast<double>(elapsedMs);
		Logger::instance()->info(QStringLiteral("[DetectionWorker]: 实际处理帧率 %1 fps（%2 帧 / %3 ms）")
			.arg(fps, 0, 'f', 1).arg(processed).arg(elapsedMs));
	}
	return true;
}

// 一个Task完成：累积结果 + 推送即时结果 + 上报进度
void DetectionWorker::finishUnit(DetectionTask& task, const cv::Mat& frame,
								 DetectionResult& result, DetectionResultSaver& saver) {
	// 累计结果(摄像头不累计)
	if (task.accumulateResults)
		task.results.push_back(result);

	// 整体处理帧率
	const float fps = task.fps.tick();

	// 进行渲染结果
	cv::Mat canvas;
	QImage image;
	if (task.renderFrames || saver.active()) {
		canvas = DetectionRenderer::renderMat(frame, result.boxes, m_config.class_names,
											  result.duration, fps);

		if (saver.active()) {
			if (saver.write(canvas)) {
				task.shared->savedFiles.fetch_add(1, std::memory_order_relaxed);
			}
			else {
				// 写失败只报一次，然后停掉保存，避免逐帧刷屏
				Logger::instance()->warning(QStringLiteral("[DetectionWorker]: 写入结果失败，已停止保存: ")
					+ saver.lastError());
				saver.end();
			}
		}
		if (task.renderFrames)
			image = DetectionRenderer::toQImage(canvas);
	}

	emit taskResult(task.jobId, result, image);		// 实时预览,逐帧推给上层

	// 当前 Task 内部的单元进度（Job 级的 task 计数由 Manager 维护）
	const int done = task.shared->currentUnitDone.fetch_add(1, std::memory_order_relaxed) + 1;
	const int total = task.shared->currentUnitTotal.load(std::memory_order_relaxed);

	// 百分比变化才上报。摄像头 total 恒为 0，percent 恒为 -1，
	// 只会在第一个单元时上报一次（lastReportedPercent 初值 -2），用来通知上层"任务已开跑"。
	const int percent = total > 0
		? static_cast<int>(static_cast<long long>(done) * 100 / total)
		: -1;
	if (percent != task.lastReportedPercent) {
		task.lastReportedPercent = percent;
		emit taskProgress(task.jobId, task.taskId, done, total);
	}
}

// 一个 Task 结束后统一上报保存结果（只报一次，避免逐帧刷屏）
void DetectionWorker::reportSaveResult(const DetectionResultSaver& saver) {
	if (saver.written() > 0) {
		Logger::instance()->success(QStringLiteral("[DetectionWorker]: 已保存 %1 个文件到 %2")
			.arg(saver.written()).arg(saver.dir()));
	}
	else if (!saver.lastError().isEmpty()) {
		Logger::instance()->warning(QStringLiteral("[DetectionWorker]: 结果保存失败: ") + saver.lastError());
	}
}
