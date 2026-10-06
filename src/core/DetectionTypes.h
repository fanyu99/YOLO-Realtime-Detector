#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>
#include <atomic>
#include <memory>
#include <vector>
#include <QtQmlIntegration/QtQmlIntegration>
#include <filesystem>
#include "Box.h"


// letterbox 预处理信息
struct LetterboxInfo {
	Q_GADGET
	QML_ELEMENT
	Q_PROPERTY(int origWidth READ origWidth)
	Q_PROPERTY(int origHeight READ origHeight)
	Q_PROPERTY(int newWidth READ newWidth)
	Q_PROPERTY(int newHeight READ newHeight)
	Q_PROPERTY(int padWidth READ padWidth)
	Q_PROPERTY(int padHeight READ padHeight)
	Q_PROPERTY(float scale READ scaleValue)
public:
	int orig_w, orig_h;	// 原图宽高
	int new_w, new_h;	// 缩放后宽高
	int pad_w, pad_h;	// 填充宽高
	float scale;		// 缩放比例

	// getter 供 QML 读取
	int origWidth() const { return orig_w; }
	int origHeight() const { return orig_h; }
	int newWidth() const { return new_w; }
	int newHeight() const { return new_h; }
	int padWidth() const { return pad_w; }
	int padHeight() const { return pad_h; }
	float scaleValue() const { return scale; }
};

// 检测器配置
struct DetectorConfig {
	Q_GADGET
	QML_ELEMENT
	Q_PROPERTY(QString modelPath READ modelPathText)
	Q_PROPERTY(QStringList classNames READ classNamesText)
	Q_PROPERTY(float confThreshold READ confThreshold)
	Q_PROPERTY(float iouThreshold READ iouThreshold)
	Q_PROPERTY(int numThreads READ numThreads)
	Q_PROPERTY(int deviceId READ deviceId)
	Q_PROPERTY(int inputWidth READ inputWidth)
	Q_PROPERTY(int inputHeight READ inputHeight)
	Q_PROPERTY(bool useCuda READ useCuda)
	Q_PROPERTY(bool valid READ isValid)
	Q_PROPERTY(bool modelExists READ model_exists)
public:
	std::filesystem::path model_path;	// 模型路径
	QVector<QString> class_names;		// 类别名称
	float conf_threshold = 0.5f;		// 置信度阈值
	float iou_threshold = 0.5f;			// NMS阈值
	int num_threads = 0;				// 线程数
	int device_id = 0;					// CUDA 设备号
	int input_width = 640;				// 推理输入宽度
	int input_height = 640;				// 推理输入高度
	bool use_cuda = true;				// 是否尝试启用 CUDA EP（失败会自动回退 CPU）

	// getter 供 QML 读取
	QString modelPathText() const { return QString::fromStdWString(model_path.wstring()); }
	QStringList classNamesText() const { return class_names; }
	float confThreshold() const { return conf_threshold; }
	float iouThreshold() const { return iou_threshold; }
	int numThreads() const { return num_threads; }
	int deviceId() const { return device_id; }
	int inputWidth() const { return input_width; }
	int inputHeight() const { return input_height; }
	bool useCuda() const { return use_cuda; }

	// 是否有效(未检查相关路径是否有效)
	bool isValid()const {
		return !model_path.empty() && !class_names.isEmpty() && conf_threshold > 0.0f && conf_threshold < 1.0f && iou_threshold > 0.0f && iou_threshold < 1.0f;
	};
	bool model_exists()const {
		return std::filesystem::exists(model_path);
	}
};

// 检测器错误
struct DetectorError {
	Q_GADGET
	QML_ELEMENT
public:
	// 错误分类
	enum class Category {
		Model,
		Config,
		Image,
		Detection,
		Other,
		None
	};
	Q_ENUM(Category)
	// 错误代码
	enum class Code {
		InvalidModelPath,	  // 模型路径无效
		InvalidConfig,		  // 配置无效
		InvalidImage,		  // 图像无效
		DetectionFailed,	  // 检测失败
		UnknownError,		  // 未知错误
		None				  // 无错误
	};
	Q_ENUM(Code)

	Q_PROPERTY(QString errorMessage READ errorMessageText)
	Q_PROPERTY(Category errorCategory READ errorCategoryValue)
	Q_PROPERTY(Code errorCode READ errorCodeValue)
	Q_PROPERTY(bool hasError READ has_error)

	QString errorMessage;
	Category errorCategory;
	Code errorCode;

	DetectorError() : errorCategory(Category::None), errorCode(Code::None) {}
	DetectorError(const QString& msg, Category category = Category::Other, Code code = Code::UnknownError)
		: errorMessage(msg), errorCategory(category), errorCode(code) {}
	bool has_error() const {
		return !errorMessage.isEmpty() && errorCategory != Category::None && errorCode != Code::None;
	}

	// getter 供 QML 读取
	QString errorMessageText() const { return errorMessage; }
	Category errorCategoryValue() const { return errorCategory; }
	Code errorCodeValue() const { return errorCode; }
};

// 兼容原有命名
using DetectorErrorCategory = DetectorError::Category;
using DetectorErrorCode = DetectorError::Code;

// 检测器警告
// 与 DetectorError 的区别：错误意味着"失败、没做成"，警告意味着"做成了，但参数被改动过"。
// 典型场景：配置的输入尺寸与 ONNX 模型实际输入不一致时，Detector 会自动按模型尺寸对齐，
// 这是一次成功的自我修正，但必须让用户显式知晓，否则界面显示的尺寸会与实际使用的尺寸分叉。
struct DetectorWarning {
	Q_GADGET
	QML_ELEMENT
public:
	// 警告代码
	enum class Code {
		InputSizeAligned,	// 配置输入尺寸与模型不符，已按模型尺寸自动对齐
		DynamicInputUsed,	// 模型输入为动态维(-1)，采用配置尺寸
	};
	Q_ENUM(Code)

	Q_PROPERTY(QString message READ messageText)
	Q_PROPERTY(Code code READ codeValue)
	Q_PROPERTY(int fromWidth READ fromWidth)
	Q_PROPERTY(int fromHeight READ fromHeight)
	Q_PROPERTY(int toWidth READ toWidth)
	Q_PROPERTY(int toHeight READ toHeight)

	QString message;						// 人类可读文案
	Code code = Code::InputSizeAligned;
	int from_width = 0, from_height = 0;	// 改动前(配置值)
	int to_width = 0, to_height = 0;		// 改动后(模型值)

	DetectorWarning() = default;
	DetectorWarning(const QString& msg, Code code_,
					int from_w = 0, int from_h = 0, int to_w = 0, int to_h = 0)
		: message(msg), code(code_), from_width(from_w), from_height(from_h),
		  to_width(to_w), to_height(to_h) {}

	// getter 供 QML 读取
	QString messageText() const { return message; }
	Code codeValue() const { return code; }
	int fromWidth() const { return from_width; }
	int fromHeight() const { return from_height; }
	int toWidth() const { return to_width; }
	int toHeight() const { return to_height; }
};

// 检测结果: 检测框 + 错误 + 推理耗时
struct DetectionResult {
	Q_GADGET
	QML_ELEMENT
	Q_PROPERTY(QVariantList boxes READ boxesVariant)
	Q_PROPERTY(DetectorError error READ errorValue)
	Q_PROPERTY(qint64 duration READ durationValue)
	Q_PROPERTY(QString source READ sourceValue)
public:
	QVector<Box> boxes;
	DetectorError error;
	long long duration = 0;	// 推理耗时(ms)
	quint64 taskId = 0;		// 任务ID(由 Worker 回填，0 表示未归属)
	QString source;			// 输入来源(图片/视频路径；摄像头为空)，由 Worker 回填，供回看时显示

	DetectionResult() = default;
	DetectionResult(const QVector<Box>& boxes_, const DetectorError& error_, long long duration_)
		: boxes(boxes_), error(error_), duration(duration_) {}

	//  getter 供 QML 读取
	QVariantList boxesVariant() const {
		QVariantList list;
		list.reserve(boxes.size());
		for (const auto& box : boxes)
			list.append(QVariant::fromValue(box));
		return list;
	}
	DetectorError errorValue() const { return error; }
	qint64 durationValue() const { return static_cast<qint64>(duration); }
	QString sourceValue() const { return source; }
};

// ============================================================================
//  作业(Job) / 任务(Task) 两级模型
//
//  DetectionJob  —— 调度与展示的单元,交由Manager进行管理：一个 Job 对应 UI 上的一个进度条
//  DetectionTask —— 真正被 worker 抢走执行的单元,交由Worker进行执行：一个 Job 可含多个 Task
//  TaskShared    —— 一个 Job 内所有 Task 共享的可变状态（可跨线程读写）,Manager可以获取相关Job的状态并上报给App
//
//  单图 / 单视频 / 摄像头：Job 只含 1 个 Task
//  批量图片：Job 含 N 个 SingleImage Task，由 Manager 分发给多个 worker
//
//  所有权规定：
//    - TaskShared 只通过 shared_ptr 使用。Manager / Job / 每个 Task 各持一份
//      拷贝，指向同一个对象；拷贝本身线程安全（引用计数是原子的）。
//    - DetectionTask 用 unique_ptr 独占：worker 消费完即销毁。
// ============================================================================

// 任务类型（任务级 / 执行层）：worker 用它决定走 runImage 还是 runStream
enum class DetectionTaskType {
	SingleImage,	// 单张图像（批量作业 = 由多个该类型 Task 组成一个 Job）
	VideoStream,	// 视频文件或摄像头流（由 cameraId 是否 >= 0 区分）
};

// 作业种类（作业级 / 展示层）：UI 用它决定怎么呈现结果、完成时发哪个信号。
//
// 为什么不复用 DetectionTaskType：
//   1. 它区分不了 Camera 和 SingleVideo —— 两者的 task.type 都是 VideoStream，
//      但对 UI 意义完全不同（摄像头没有"终态结果"，只能靠取消结束）。
//   2. 它表达不了 Batch —— 批量作业里每个 Task 的类型可以不同（图片视频混排），
//      没有唯一的 TaskType 可反推。
// 所以这两个枚举是互补关系，不是重复。
enum class DetectionJobType {
	SingleImage,	// 一张图   -> 完成后给一张结果图
	SingleVideo,	// 一个视频 -> 完成后给一串帧结果
	Camera,			// 摄像头流 -> 结果靠 frameReady 逐帧推，正常只能靠取消结束
	Batch,			// 批量     -> 结果靠 frameReady + 落盘，终态只报汇总
};

// 任务状态
enum class DetectionTaskStatus {
	Pending,
	Running,
	Cancelled,
	Completed,
	Failed
};

// 一个 Job 内所有 Task 共享的状态。
// 会变的字段一律 std::atomic；构造后不再变的字段保持普通类型。
// 含原子成员，既不可拷贝也不可移动 —— 只能通过 shared_ptr 使用。
//
// 进度分两层，但统一成一个公式：
//   progress = (已完成 task 数 + 当前 task 内部的单元比例) / task 总数
//     - 批量作业：处理完 2 个、第 3 个到一半 → (2 + 0.5) / 4 = 62.5%
//     - 单个视频：跑到第 450 / 1000 帧      → (0 + 0.45) / 1 = 45%
//   公式是单调的：当前 task 完成时 completedTasks++ 且单元比例归零，总和仍然上升。
struct TaskShared {
	TaskShared(quint64 jobId_, DetectionJobType jobType_, const QString& saveDir_ = QString())
		: jobId(jobId_), jobType(jobType_), saveDir(saveDir_) {}

	const quint64 jobId;								// 只读
	const DetectionJobType jobType;						// 只读：作业种类，供 Manager 在收尾时回传给 UI
	// 只读：结果落盘目录（**已含 detection_results 子目录**，由 App 侧用
	// DetectionResultSaver::resultsDirFor() 组合好传进来，约定只在一处定义）。空 = 本次不保存
	const QString saveDir;

	// ---- Job 级：由 Manager 维护 ----
	std::atomic<int>  totalTasks{ 0 };					// 本 Job 的 Task 总数（投递前设定）
	std::atomic<int>  completedTasks{ 0 };				// 已结束(完成或失败)的 Task 数
	std::atomic<int>  failedTasks{ 0 };					// 其中失败的 Task 数
	std::atomic<int>  savedFiles{ 0 };					// 已成功写出的结果文件数（worker 累加，供 UI 汇总）

	// ---- Task 级：由正在执行的那个 worker 维护（单 worker 下同一时刻只有一个）----
	std::atomic<int>  currentUnitDone{ 0 };				// 当前 Task 内部已完成的工作单元
	std::atomic<int>  currentUnitTotal{ 0 };			// 0 表示总量未知（摄像头流）

	std::atomic<bool> cancelRequested{ false };			// 取消标志（worker 逐单元检查）
	std::atomic<DetectionTaskStatus> status{ DetectionTaskStatus::Pending };

	// 请求取消：Manager 在 GUI 线程调用，worker 在 worker 线程读取
	void requestCancel() {
		cancelRequested.store(true, std::memory_order_relaxed);
		status.store(DetectionTaskStatus::Cancelled, std::memory_order_relaxed);
	}
	bool cancelled() const {
		return cancelRequested.load(std::memory_order_relaxed);
	}

	// 一个 Task 结束、下一个 Task 开始前调用：把上一个 Task 的单元进度清掉
	void resetCurrentUnit() {
		currentUnitDone.store(0, std::memory_order_relaxed);
		currentUnitTotal.store(0, std::memory_order_relaxed);
	}

	// 进度 0-1
	float progress() const {
		const int tasks = totalTasks.load(std::memory_order_relaxed);
		if (tasks <= 0) return 0.0f;

		const int ut = currentUnitTotal.load(std::memory_order_relaxed);
		const float frac = ut > 0
			? static_cast<float>(currentUnitDone.load(std::memory_order_relaxed)) / static_cast<float>(ut)
			: 0.0f;

		const float p = (static_cast<float>(completedTasks.load(std::memory_order_relaxed)) + frac)
			/ static_cast<float>(tasks);
		return p < 0.0f ? 0.0f : (p > 1.0f ? 1.0f : p);
	}

	// 只有"整个 Job 就一个 Task、且它总量未知"（摄像头流）才算不确定进度
	bool indeterminate() const {
		return totalTasks.load(std::memory_order_relaxed) == 1
			&& currentUnitTotal.load(std::memory_order_relaxed) == 0;
	}
};
using TaskSharedPtr = std::shared_ptr<TaskShared>;

// 整体处理帧率计数器：用相邻 tick() 的时间间隔估算帧率，再以指数移动平均(EMA)平滑。
// 该值反映工作单元的处理节奏，不等同于模型单独的推理帧率。
// 首次 tick() 返回初始显示值；之后每完成一个工作单元采样一次。
// 只由执行该 Task 的那个 worker 线程读写，不需要原子量。
struct FpsCounter {
	float alpha = 0.15f;		// 平滑系数：越小越稳、越迟钝（0.1 ~ 0.3 比较合适）
	float initialFps = 30.0f;	// 初始帧率：避免启动时从 0 一路往上爬
	float fps = initialFps;		// 当前平滑帧率

	void reset() {
		fps = initialFps;
		m_clock.invalidate();
	}

	// 一个工作单元完成时调用一次，返回平滑后的帧率
	float tick() {
		if (!m_clock.isValid()) {						// 第一次：只起表，先返回初始值
			m_clock.start();
			return fps;
		}
		const qint64 ms = m_clock.restart();
		if (ms <= 0)
			return fps;

		const float instant = 1000.0f / static_cast<float>(ms);		// 瞬时帧率
		fps = alpha * instant + (1.0f - alpha) * fps;				// EMA 平滑
		return fps;
	}

private:
	QElapsedTimer m_clock;		// 上一次采样时刻
};

// 一个可执行的任务单元
struct DetectionTask {
	quint64 taskId = 0;
	quint64 jobId = 0;								// 冗余一份，便于日志与信号路由
	DetectionTaskType type = DetectionTaskType::SingleImage;
	QString source;									// 图片 / 视频路径；摄像头时为空
	int cameraId = -1;								// >= 0 表示摄像头设备号
	bool accumulateResults = true;					// 摄像头流应设 false，避免结果无限增长
	bool renderFrames = true;						// 是否在 worker 线程内画框并转 QImage（离线批量作业可关掉省算力）
	TaskSharedPtr shared;							// 与 Job / Manager 共享的同一份状态
	QVector<DetectionResult> results;				// 仅 accumulateResults 为 true 时累积

	// 只由执行该 Task 的 worker 线程读写 :
	int lastReportedPercent = -2;					// 上次已上报的百分比（-2 表示尚未上报）
	FpsCounter fps;									// 整体处理帧率（画在画面左上角）
};

// 一个作业：持有共享状态与它的任务列表
struct DetectionJob {
	quint64 jobId = 0;
	DetectionJobType type = DetectionJobType::SingleImage;	// 作业种类(展示层)，由工厂从 task 派生
	TaskSharedPtr shared;							// Manager 与各 Task 共享
	std::vector<std::unique_ptr<DetectionTask>> tasks;
};

// 单任务作业的种类：从 task 派生，**不独立判断** ——
// 这样"什么算视频"只有 isVideoPath() 一处定义，不会出现 job.type 与 task.type 漂移。
// 注意方向是单向的(Task -> Job)；反过来在 Batch 下不成立(每个 task 类型可不同)。
inline DetectionJobType jobTypeOf(const DetectionTask& task) {
	if (task.cameraId >= 0)
		return DetectionJobType::Camera;
	return task.type == DetectionTaskType::VideoStream ? DetectionJobType::SingleVideo
													   : DetectionJobType::SingleImage;
}

// 构造一个「单 Task」作业：单图 / 单视频 / 摄像头
// saveDir 为空表示本次不保存结果。
inline DetectionJob makeSingleTaskJob(quint64 jobId, quint64 taskId,
									  DetectionTaskType type,
									  const QString& source = QString(),
									  int cameraId = -1,
									  const QString& saveDir = QString())
{
	DetectionJob job;
	job.jobId = jobId;

	auto task = std::make_unique<DetectionTask>();
	task->taskId = taskId;
	task->jobId = jobId;
	task->type = type;
	task->source = source;
	task->cameraId = cameraId;
	task->accumulateResults = (cameraId < 0);	// 摄像头流不累积，靠 taskResult 逐帧推送

	job.type = jobTypeOf(*task);				// 先定 task.type，再派生 job.type
	job.shared = std::make_shared<TaskShared>(jobId, job.type, saveDir);
	task->shared = job.shared;

	job.tasks.push_back(std::move(task));
	return job;
}

// ---------------------------------------------------------------------------
//  输入类型判断（只看后缀，不访问磁盘）
//  放在这里是为了让"什么算视频"只有一个定义 —— App 和 makeBatchJob 共用它。
// ---------------------------------------------------------------------------
inline bool isVideoPath(const QString& path) {
	static const QStringList kVideoExts = {
		QStringLiteral(".mp4"), QStringLiteral(".avi"), QStringLiteral(".mov"),
		QStringLiteral(".mkv"), QStringLiteral(".wmv")
	};
	const QString lower = path.toLower();
	for (const QString& ext : kVideoExts)
		if (lower.endsWith(ext))
			return true;
	return false;
}

inline bool isImagePath(const QString& path) {
	static const QStringList kImageExts = {
		QStringLiteral(".jpg"), QStringLiteral(".jpeg"), QStringLiteral(".png"),
		QStringLiteral(".bmp"), QStringLiteral(".webp")
	};
	const QString lower = path.toLower();
	for (const QString& ext : kImageExts)
		if (lower.endsWith(ext))
			return true;
	return false;
}

// 构造一个「批量」作业：图片 / 视频按扩展名逐个判断，可以混排。
// 不设置 taskId（留 0）—— id 分配统一由 Manager::submit() 负责，避免两处各自编号。
//
// 两个刻意的选择：
//   accumulateResults = false —— 批量作业结果不进内存。图片批量尚可，视频批量会
//     把 results 撑爆（几千帧 × 每帧一个 DetectionResult）。结果改走 frameReady
//     逐单元推送，需要留档则由保存功能落盘。
//   renderFrames = true —— 批量就是要逐个看画面。
// saveDir 为空表示本次不保存结果。
inline DetectionJob makeBatchJob(quint64 jobId, const QStringList& paths,
								 const QString& saveDir = QString()) {
	DetectionJob job;
	job.jobId = jobId;
	job.type = DetectionJobType::Batch;			// 批量无法从单个 task 派生，显式指定
	job.shared = std::make_shared<TaskShared>(jobId, job.type, saveDir);

	for (const QString& path : paths) {
		auto task = std::make_unique<DetectionTask>();
		task->jobId = jobId;
		task->source = path;
		// 既不是图片也不是视频的路径仍然建成 SingleImage 任务，
		// 由 worker 在执行时报"图片加载失败" —— 一个坏文件不该让整批失败
		task->type = isVideoPath(path) ? DetectionTaskType::VideoStream
									   : DetectionTaskType::SingleImage;
		task->cameraId = -1;
		task->accumulateResults = false;
		task->renderFrames = true;
		task->shared = job.shared;

		job.tasks.push_back(std::move(task));
	}
	return job;
}
