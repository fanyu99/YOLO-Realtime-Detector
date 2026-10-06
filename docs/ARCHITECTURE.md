# 架构说明 / Architecture Notes

本文档记录各模块的职责、线程模型与关键设计取舍。英文 README 的架构小节是这里的摘要版。

---

## 1. 分层

```
QML (qml/)  →  应用层 (src/app/)  →  核心层 (src/core/)  →  ONNX Runtime / OpenCV
```

依赖方向严格单向：`core` 不认识 `app`，`app` 不认识 QML 的具体控件。QML 只通过
`DetectionApp` 暴露的属性与 `Q_INVOKABLE` 方法与后端交互。

| 层 | 目录 | 是否依赖 Qt GUI | 是否 `QObject` |
|---|---|---|---|
| 核心层 | `src/core/` | 否（`DetectionRenderer` 用 `QImage`，属 QtGui） | 否 |
| 应用层 | `src/app/` | 是 | 是 |
| 界面层 | `qml/` | 是 | — |

---

## 2. 线程模型

整个程序只有两条长期存在的线程：

1. **GUI 线程** —— 运行 QML 场景图、`DetectionApp`、`DetectionManager`。
2. **worker 线程** —— `DetectionWorker::work()` 循环，串行消费任务队列。

`Detector` 对象**在 worker 线程内构造并析构**，因此它内部持有的 ONNX 会话、CUDA
context 等资源从不跨线程。

跨线程数据流只有两个方向，且都遵守「不共享可变状态」的原则：

- **GUI → worker**：投递 `std::unique_ptr<DetectionTask>`（经互斥量保护的队列 +
  条件变量唤醒）。任务内的 `TaskShared` 通过 `shared_ptr` 共享，其中所有会变的字段都是
  `std::atomic`，这是唯一被允许跨线程直接读写的结构。
- **worker → GUI**：Qt 信号（`taskResult` / `taskProgress` / `taskCompleted` …），
  由 Qt 自动排队到 GUI 线程。携带的 `QImage` 是隐式共享的，跨线程只是加一次引用计数。

`DetectionImageProvider::requestImage()` 可能被场景图渲染线程调用，所以其内部用
`QMutex` 保护帧数据；帧一旦生成就不再修改，因此共享是安全的。

---

## 3. 作业（Job）与任务（Task）

```
DetectionJob   一个 UI 进度条 = 一个 Job
 ├─ TaskShared        Job 内所有 Task 共享的可变状态（原子量）
 └─ DetectionTask*    真正被 worker 执行的最小单元（unique_ptr 独占）
```

- 单图 / 单视频 / 摄像头：Job 只含 1 个 Task。
- 批量：Job 含 N 个 Task，图片与视频可混排。

**进度公式**统一为：

```
progress = (已结束 Task 数 + 当前 Task 内部单元比例) / Task 总数
```

- 批量：处理完 2 个、第 3 个到一半 → `(2 + 0.5) / 4 = 62.5%`
- 单视频：跑到第 450 / 1000 帧 → `(0 + 0.45) / 1 = 45%`

公式单调：当前 Task 完成时 `completedTasks++` 且单元比例归零，总和仍然上升。

**两个枚举是互补的，不是重复：**

| 枚举 | 层级 | 用途 |
|---|---|---|
| `DetectionTaskType` | 任务级 / 执行层 | worker 据此决定走 `runImage` 还是 `runStream` |
| `DetectionJobType` | 作业级 / 展示层 | UI 据此决定如何呈现结果、完成时发哪个信号 |

`DetectionTaskType` 区分不了「摄像头」与「单视频」（两者都是 `VideoStream`），也表达不了
「批量」（每个 Task 类型可不同）。因此单 Task 作业的 `job.type` 由 `jobTypeOf(task)`
**单向派生**，保证「什么算视频」只在 `isVideoPath()` 一处定义。

---

## 4. 核心层模块

### `Detector`（`Detector.h` / `Detector.cpp`）

ONNX 会话的封装，负责预处理、推理与后处理。

- **非 `QObject`**：错误通过 `lastError()` 上抛，警告通过 `warnings()`，结果只走返回值。
  因此可在任意线程使用，无需 `moveToThread`。
- `CreateSessionOptions()` 尝试启用 CUDA EP；失败时把 `cuda_ok` 置 false 并回退 CPU，
  `device()` / `usingCUDA()` 反映**实际**生效的设备。
- `alignInputSizeToModel()` 读取模型真实输入 shape 校正配置；模型为动态维（`-1`）时以
  配置尺寸为准，并生成 `DynamicInputUsed` 警告。
- `letterbox()` → `preprocess()`（BGR→RGB、归一化、HWC→NCHW）→ `runInference()` →
  `parse_yolo_ouput()` → `map_boxes_to_original_image()`。
- `parse_yolo_ouput()` 的 `num_anchors` **必须由输出张量形状反推**，不能写死 8400：
  8400 只在 640×640 下成立（320 → 2100，1280 → 33600）。
- `runInference()` 内部 try/catch，任何异常都转成错误结果返回，**绝不让异常逃出
  `Detector`**（否则会穿到线程入口触发 `std::terminate`）。
- `warmup()` 用假输入跑若干次 `Run`，提前烧掉 CUDA context、cuDNN handle、内存 arena
  首次分配等一次性开销；失败只记日志，不影响后续检测。

### `DetectionTypes.h`

类型中枢：`LetterboxInfo`、`DetectorConfig`、`DetectorError`、`DetectorWarning`、
`DetectionResult`、`TaskShared`、`FpsCounter`、`DetectionTask`、`DetectionJob`，
以及 `jobTypeOf()` / `makeSingleTaskJob()` / `makeBatchJob()` / `isVideoPath()` /
`isImagePath()`。

其中 `isVideoPath()` / `isImagePath()` 只按后缀判断、不访问磁盘 —— 目的是让「什么算视频」
只有一个定义，供 `DetectionApp` 与 `makeBatchJob()` 共用。

### `Box.h` / `AppError.h`

QML 可见的值类型（`Q_GADGET` + `QML_ELEMENT`），提供 `Box2Rect` / `Rect2Box` 坐标互转。

### `ImageShower`（`src/tools/`）

基于 `cv::imshow` 的独立预览工具，与主界面无关，供 `main.cpp` 中已注释的演示函数使用。

---

## 5. 应用层模块

### `DetectionApp` —— QML 门面

把 QML 的交互翻译成对 `DetectionManager` 的调用，并把进度 / 结果 / 错误 / 日志回传 QML。

- 实现 `QQmlParserStatus`：构造函数里对象尚未与 engine 关联，必须等 `componentComplete()`
  才能拿到 engine 注册 image provider。
- 只认不透明的 `jobId`，不接触 `DetectionTask` / `TaskShared`。
- 刻意**不保存**「当前作业是什么种类」：种类随 `jobFinished` 一起回来，因此不存在需要跨
  事件同步维护的字段（早期版本用 `JobType` + `IsBatch` 两个字段，实测有 12 个维护点且漏清
  过一处）。
- 横幅（banner）只有一个槽位，避免多条堆叠；`BannerWarning` 用于「输入尺寸已自动对齐」这类
  非致命提示，`BannerSuccess` 用于「结果已保存」。
- 回看只推进 UI 侧索引，**完全不碰 worker**。

### `DetectionManager` —— 作业调度

对上提供「提交 / 取消 / 查进度」与作业级信号；对下把 Job 拆成 Task 投递给 worker，并把
worker 的上报汇总成 job 级事件。

- `applyConfig()` 内部完成「停旧 worker + 用新配置建新 worker」。ONNX 会话一旦建立就无法
  替换模型，改阈值也得重建，所以「改配置」必然等于「重建」。
- `jobSaved` 在 `jobFinished` / `jobFailed` / `jobCancelled` **之前**发出 —— 因为 `App` 在
  终态里会清掉 `currentJobId`，晚发就可能被丢掉。
- `m_startedJobs` 保证一个 Job 只发一次 `jobStarted`。

### `DetectionWorker` —— 执行单元

在一条独立线程里跑 `work()` 循环，从队列取 Task 执行。

- `setConfig()` 必须在 `start()` 之前调用（会话不可替换）。
- `stop()` 会丢弃队列中尚未开始的 Task。
- 区分 `runImage()` 与 `runStream()`；`finishUnit()` 负责单帧的「渲染 → 计数 → 推送」。
- `failTaskIfUnfinished()` 在任务抛异常时补发失败信号，避免重复发。

### `DetectionRenderer`

把「帧 + 检测框」渲染成可直接给 QML 的 `QImage`。

渲染**必须**放在 worker 线程：`cv::Mat` 浅拷贝共享像素，跨线程传 `Mat` 等于传一块会被下一帧
覆写的内存。在 worker 线程内一次性画好框并转成 `QImage`，上层拿到的就是一份独立、可安全
显示的画面。

- `renderMat()` 返回 `cv::Mat`（保存视频时必须用这个，`VideoWriter` 吃 `Mat`）；
- `render()` 在 `renderMat()` 基础上转 `QImage`（显示路径用这个）。

### `DetectionResultSaver`

目录约定：`<用户选的根目录>/detection_results/`，图片写 `<原文件名>.jpg`，视频/摄像头写
`<原文件名>.mp4`。只保存画了框的画面，不写 labels。

只在 worker 线程内使用，不跨线程，因此不需要锁。保存失败不抛异常、也不中断检测：错误记在
`lastError()` 里，由调用方上报一次。

### `DetectionImageProvider`

既服务「实时画面」（`image://detection/latest?rev=N`），也服务「结果回看」
（`image://detection/frame/<seq>`）。

回看缓冲是**有界环形**（`kMaxReviewFrames = 60`）。关键设计：推理线程不会因为 UI 看得慢而
被阻塞，UI 只按自己的节奏推进索引 —— 两边彻底解耦，也就不需要任何跨线程的「等待 / 放行」
标志位。回看副本按需缩小（1920×1080 × 60 帧约 370 MB，缩到 1280 宽后约 165 MB）。

### `Logger`

Qt 风格日志器：通过 `qInstallMessageHandler` 接管 Qt 自身的 `qDebug` / `qInfo` /
`qWarning` / `qCritical`，统一汇入本日志器，再以 `messageLogged` 信号推给 QML 日志面板。
`main()` 里尽早调用 `Logger::instance()`，保证启动阶段的日志也能进面板。

---

## 6. 界面层

| 文件 | 职责 |
|---|---|
| `main.qml` | 主窗口：顶部标题栏、警告/成功横幅、左侧常驻配置栏 + 结果输出 + 状态栏、右侧工具栏 + 结果区 + 日志区 |
| `Theme.qml` | 配色单例，跟随系统深浅色 |
| `LogView.qml` | 日志列表，按级别着色 |
| `ConfigDialog.qml` | 模型与检测参数表单，确定后调用 `app.setConfig(...)` |
| `CameraDialog.qml` | 摄像头设备选择，确定后调用 `app.onDetectCameraClicked(id)` |

结果画面用 `Image` 绑定 `app.frameSource()`：

- `source` 必须带上会变化的 `rev`，否则 QML 会因为 URL 完全相同而吃缓存；
- `cache` 必须为 `false`，否则每个 `rev` 都会在图片缓存里留一份，内存只涨不降；
- `asynchronous` 必须为 `false`，否则异步取图会乱序，画面会跳帧。

检测开始后主动暂停原始视频预览（`mediaPlayer.pause()`），避免两路视频同时解码。

---

## 7. 构建系统

- 三个静态库：`Detector`（核心）、`ImageShower`（工具）、`DetectionWorker`（执行单元 +
  渲染 + 落盘），加上主可执行程序 `YOLO`。
- QML 模块 `URI yolo`，`RESOURCE_PREFIX "/qt/qml/"`。QML 源文件位于 `qml/`，通过
  `QT_RESOURCE_ALIAS` 把资源路径压平为 `/qt/qml/yolo/<文件名>.qml`，从而与文件物理位置解耦，
  `engine.load("qrc:/qt/qml/yolo/main.qml")` 不受目录调整影响。
- `src`、`src/core`、`src/app`、`src/tools` 统一加入 include 搜索路径，各模块之间以裸文件名
  互相 `#include`，源文件在 `src/` 内挪动时无需改写 include。
- Windows 下构建后自动把依赖目录中的 DLL 复制到 exe 旁（用 glob，新增依赖无需改脚本）；
  CUDA/cuDNN 运行库由 `DEPLOY_CUDA_RUNTIME` 选项控制。

---

## 8. 扩展点

- **多 worker 并行**：`DetectionManager::m_worker` 目前是单个 `unique_ptr`，注释已注明
  「单路输入先用 1 个 worker」。改为 `std::vector` 并在 `dispatch()` 中轮询/按负载分配即可，
  两级 Job/Task 模型本身已支持多 worker 消费同一个 Job 的多个 Task。
- **新增输入类型**：在 `DetectionTypes.h` 扩展 `DetectionTaskType` 与 `isVideoPath()` /
  `isImagePath()`，并在 `DetectionWorker::execute()` 中分派。
- **新增后处理**：`Detector::parse_yolo_ouput()` 目前针对 YOLO 检测头，分割/姿态需另加解析
  函数并在 `runInference()` 中分支。
