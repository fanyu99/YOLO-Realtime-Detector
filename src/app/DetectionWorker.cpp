#include "DetectionWorker.h"
#include "DetectionRenderer.h"
#include "DetectionResultSaver.h"
#include <vector>
#include <QElapsedTimer>
#include <opencv2/opencv.hpp>
#include "Logger.h"

DetectionWorker::DetectionWorker(QObject* parent) : QObject(parent) {}

DetectionWorker::~DetectionWorker() {
	stop();
}

void DetectionWorker::setConfig(const DetectorConfig& config) {
	if (m_running.load(std::memory_order_relaxed)) {
		Logger::instance()->warning(QStringLiteral("[DetectionWorker]: 工作线程运行中，忽略配置变更"));
		return;
	}
	m_config = config;
}

bool DetectionWorker::start() {
	if (m_running.load(std::memory_order_relaxed))
		return true;							// 已在运行

	if (m_config.model_path.empty()) {
		Logger::instance()->error(QStringLiteral("[DetectionWorker]: 未设置检测器配置，无法启动"));
		return false;
	}

	m_running.store(true, std::memory_order_relaxed);

	// QThread::create 里的函数直接在【新线程】里执行。
	// 不要用 connect(thread, &QThread::started, this, lambda) 那种写法：
	// 当 receiver(this) 的线程亲和性是 GUI 线程时，AutoConnection 会退化成
	// QueuedConnection，work() 会被投递回 GUI 线程执行，界面直接卡死。
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
		// 线程已结束：复位运行标志，让 isRunning() 如实反映状态，
		// 避免后续 start() 误以为它还在跑，把任务投进一个已经死掉的队列
		m_running.store(false, std::memory_order_relaxed);
	}));
	if (m_thread)m_thread->start();

	Logger::instance()->info(QStringLiteral("[DetectionWorker]: 工作线程已启动"));
	return true;
}

void DetectionWorker::stop() {
	if (!m_thread)
		return;

	std::vector<quint64> dropped;
	m_running.store(false, std::memory_order_relaxed);
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		// 丢弃尚未开始的 Task：它们永远不会被执行，必须让 Manager 知道
		while (!m_queue.empty()) {
			dropped.push_back(m_queue.front()->jobId);
			m_queue.pop();
		}
	}
	m_cv.notify_all();							

	for (quint64 jobId : dropped)				// 信号一律在锁外发
		emit taskCancelled(jobId);

	m_thread->wait();						
	m_thread.reset();							
	m_detector.reset();						

	Logger::instance()->info(QStringLiteral("[DetectionWorker]: 工作线程已停止"));
	emit stopped();
}

bool DetectionWorker::submit(std::unique_ptr<DetectionTask> task) {
	if (!task)
		return false;

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_running.load(std::memory_order_relaxed))
			return false;
		m_queue.push(std::move(task));
	}
	m_cv.notify_one();
	return true;
}

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
		// 这是"成功但改了参数"，用户需要知道，否则界面显示的尺寸会与实际使用的分叉
		for (const DetectorWarning& warning : m_detector->warnings()) {
			Logger::instance()->warning(warning.message);
			emit detectorWarning(warning);
		}
		Logger::instance()->info(QStringLiteral("[DetectionWorker]: 检测器就绪，运行设备: ")
			+ QString::fromUtf8(m_detector->device()));

		// 预热：把 ONNX Runtime / CUDA EP 的首次初始化开销
		// 提前到"用户还没提交检测任务"的时刻，否则这笔一次性成本会落在第一帧上。
		// 放在警告之后：警告里的尺寸对齐会改变 config，预热必须用对齐后的尺寸。
		m_detector->warmup(3);
	}
	emit started();

	while(true) {
		std::unique_ptr<DetectionTask> task;
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			m_cv.wait(lock, [ this ]() {
				// 任务队列不为空或者停止时不阻塞
				return !m_running.load(std::memory_order_relaxed) || !m_queue.empty();
				});
			if (!m_running.load(std::memory_order_relaxed))
				break;							
			task = std::move(m_queue.front());
			m_queue.pop();
		}
		// 单任务级兜底：一个坏帧/坏任务不该带走整个 worker。
		// 没有这层的话，异常会被线程入口接住，worker 静默死亡，
		// 而该 Job 永远收不到终态信号 —— Manager 会一直以为"有作业在跑"，
		// 后续改配置会被永久拒绝。
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

// 补发失败信号：只在任务还没落到终态时才发，
// 避免和 execute() 已经发过的 taskCompleted/taskFailed/taskCancelled 重复，
// 否则 Manager 的 retireTask 会被多减一次，误删别的作业。
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
	else {
		switch (task.type) {
		case DetectionTaskType::SingleImage:	ok = runImage(task);  break;
		case DetectionTaskType::VideoStream:	ok = runStream(task); break;
		}
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

// 单张图像：一个工作单元
bool DetectionWorker::runImage(DetectionTask& task) {
	// 一张图 = 一个工作单元
	task.shared->currentUnitTotal.store(1, std::memory_order_relaxed);

	// 自己读一次图：Detector::detect(QString) 内部也是 imread，
	// 这里把 Mat 留在手上，才能在 worker 线程里画框转 QImage
	cv::Mat frame = cv::imread(task.source.toStdString());
	if (frame.empty()) {
		m_lastError = DetectorError(QStringLiteral("[DetectionWorker]: 图片加载失败: ") + task.source,
			DetectorErrorCategory::Image, DetectorErrorCode::InvalidImage);
		return false;
	}

	DetectionResult result = m_detector->detect(frame);
	if (result.error.has_error()) {
		m_lastError = result.error;
		return false;
	}
	result.taskId = task.taskId;
	result.source = task.source;   // 回看时要知道这是哪个输入的结果

	// 保存（未开启保存时 saver 全程 no-op）
	DetectionResultSaver saver;
	if (!saver.begin(task, frame.cols, frame.rows, 0.0) && !saver.lastError().isEmpty())
		Logger::instance()->warning(QStringLiteral("[DetectionWorker]: ") + saver.lastError());
	finishUnit(task, frame, result, saver);
	reportSaveResult(saver);
	return true;
}

// 视频文件 / 摄像头流：逐帧推进，帧就是工作单元
bool DetectionWorker::runStream(DetectionTask& task) {
	cv::VideoCapture cap;
	if (task.cameraId >= 0)
		cap.open(task.cameraId);
	else
		cap.open(task.source.toStdString());

	if (!cap.isOpened()) {
		m_lastError = DetectorError(QStringLiteral("[DetectionWorker]: 无法打开视频源!"),
			DetectorErrorCategory::Detection, DetectorErrorCode::DetectionFailed);
		return false;
	}

	// 摄像头：让驱动只缓 1 帧。
	// 推理慢于出帧时，cap.read() 会一直返回【最旧的】缓存帧，端到端延迟无限累积。
	// 必须在 open() 之后设；部分后端（如 Windows 的 MSMF）会忽略它，set() 返回 false
	// 就说明没生效 —— 那种情况只能改用"单槽采集线程"的方案。
	// 视频文件不需要：按需解码不存在积压，而且视频检测要求每一帧都不漏。
	if (task.cameraId >= 0) {
		if (cap.set(cv::CAP_PROP_BUFFERSIZE, 1))
			Logger::instance()->info(QStringLiteral("[DetectionWorker]: 摄像头缓冲已设为 1 帧"));
		else
			Logger::instance()->warning(QStringLiteral(
				"[DetectionWorker]: 当前后端不支持设置缓冲帧数，实时预览可能逐渐滞后"));
	}

	// 预读总帧数：摄像头返回 0，代表总量未知（UI 显示忙碌）
	const int frameCount = task.cameraId >= 0 ? 0 : static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT));
	task.shared->currentUnitTotal.store(frameCount, std::memory_order_relaxed);

	// 保存（未开启保存时 saver 全程 no-op）。VideoWriter 需要帧尺寸与帧率，
	// 用 cap.get() 而不是等第一帧，这样 VideoWriter 能在循环开始前就打开。
	DetectionResultSaver saver;
	const int outW = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
	const int outH = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
	if (!saver.begin(task, outW, outH, cap.get(cv::CAP_PROP_FPS)) && !saver.lastError().isEmpty())
		Logger::instance()->warning(QStringLiteral("[DetectionWorker]: ") + saver.lastError());

	cv::Mat frame;
	int processed = 0;
	QElapsedTimer clock;
	clock.start();

	while (cap.read(frame)) {
		// 逐帧检查取消
		if (task.shared->cancelled())
			break;								// 取消按正常结束处理，由 execute() 发 taskCancelled
		if (frame.empty())
			break;

		DetectionResult result = m_detector->detect(frame);
		if (result.error.has_error()) {
			m_lastError = result.error;
			return false;
		}
		result.taskId = task.taskId;
		result.source = task.source;
		finishUnit(task, frame, result, saver);
		++processed;
	}

	reportSaveResult(saver);

	// 报告实际处理帧率：摄像头用它判断有没有跟上出帧速度。
	// 明显低于摄像头标称帧率（比如 30fps 的摄像头只跑出 8fps）就说明方案 A 不够，
	// 需要换成"单槽采集线程"才能把延迟压住。
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

	// 整体处理帧率：按相邻工作单元完成的间隔估算并做 EMA 平滑。
	// 间隔包含读帧、检测，以及前一帧的渲染等处理开销；不是单独的模型推理耗时。
	const float fps = task.fps.tick();

	// 需要显示或需要保存时都得渲染。
	// 注意：在 worker 线程内画框并转 QImage —— cv::Mat 是浅拷贝共享像素的，
	// 直接跨线程传 Mat 等于传一块会被下一帧覆写的内存。
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
