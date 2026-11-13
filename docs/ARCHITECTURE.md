# 架构说明 / Architecture Notes

本文档记录各模块的职责、线程模型与关键设计取舍。英文 README 的架构小节是这里的摘要版。


---

后续整体推理架构更改：
- Worker执行最小单元Task,Manager管理一个Job的状态
一次会话对应一个Job（同一个模式复用）
后续需要将Worker执行单元更改为Job而不是task,Worker的队列需要更改为Job队列，
Manager管理Job的状态，Worker执行Job中的Task

APP（QML点击对应的按钮,调用manager中的创建会话Session（通过Worker进行创建））
-> Manager（管理Worker和Session状态）-> Worker（线程内创建Session）
-> Session（创建配对的FrameSource和DetectionFlow）
-> FrameSource（获取帧）
-> DetectionFlow（预处理+推理+解析+NMS）-> Manager（汇总结果）-> APP（QML显示结果）
Manager调用Session的创建帧源方法获取当前的模式（CPU/GPU）的帧源（image/video/camera等）
Session创建对应的Flow（CPU/GPU）进行配对，Session统一管理FrameSource和DetectionFlow的生命周期

```
DetectionManager
    -> 管理 DetectionJob
    -> 管理 Job 状态和 fallback

DetectionWorker
    -> 一次执行一个 Job
    -> Job 开始时创建 DetectionSession
    -> Job 内依次执行多个 DetectionTask
    -> Job 结束时销毁 DetectionSession

DetectionSession
    -> 保存 Job 级模式和配置
    -> 持有一个 CpuDetectionFlow 或 GpuDetectionFlow
    -> 持有来源创建策略
    -> 不负责 UI 和 Job 统计

DetectionTask
    -> 持有自己的 FrameSource
    -> 使用所属 Session 的 Flow
    -> 负责具体图片/视频/摄像头执行

```

---

当前完成：

- 新增 `FrameHandle` 抽象类及 `CpuFrameHandle`，并接入 `FrameSource`，统一帧句柄管理。
- 新增 `CpuPreprocessor`，统一预处理流程；将 `DetectorError` 更名为 `DetectionError`，新增 `PreprocessFailed` 错误类型。
- 完成 `CpuDetector` 纯化解耦，负责 ORT 会话和推理，并接入 `CpuDetectionFlow`。
- 新增 `CommonPostProcessor`，完成 CPU 推理流程中的预处理、推理和后处理。
- CPU 推理总线已接入 `DetectionWorker`。
- `FfmpegFrameSource` 支持摄像头等设备，并接入 CPU 推理总线，QML 可使用设备帧。
- 完成 `FrameSource` 抽象工厂。
- 新增 `DetectionSession`，并接入 Worker 和 Manager 基线。
- 接线fallback机制，fallback信号层层上报并使用自愈机制完成配置的修正
- 完成当前的fallback机制的QML层接线

- 调整帧源结构：
  - `FileFrameSource` 更名为 `ImageFrameSource`，仅支持单图（固定使用CPU的opencv的imread）
  - `FfmpegFrameSource` 负责摄像头和视频文件的软解、硬解。
  - `HikCameraFrameSource` 负责海康相机。
  - 工厂根据输入类型创建对应帧源。
- App层的创建帧源函数删除，并将创建帧源的功能放置在Session中，App/Manager层只进行提交帧源描述，由Worker来进行创建对应的帧源回填到task中
- App 层 onDetectFileClicked 中视频分支调用 makeFileSource 的问题仍在——视频会拿到 ImageFrameSource。
后续把 App 层接入工厂时，正好可以用新的 isVideoPath → createVideoFrameSource 补上这个缺口。


- GPU处理流程：
   - 新增`CudaDevice`作为进程级的单例设备管理，负责将CUDA/FFmpeg的硬件上下文初始化并提供ffmpeg硬件上下文的引用`BufferRef*`
   ;注意：`CudaDevice`需要在创建Worker的时候进行创建，Worker在启动工作循环/创建Session的时候使用`CudaDevice`的`use()`设置设备
   - 新增`GpuFrameHandle`类，用于封装GPU帧数据`AVFramePtr`
   - 新增`FfmpegDecoder`中的GPU解码路径处理（从`CudaDevice`获取hwDevice引用，并设置显存缓冲池以及硬件帧上下文到设备上下文）；
   - 在`FfmpegFrameSource`中添加使用GPU的路径（设置decodeConfig中的useGpuDecode为true）
- 完善 `GpuFrameHandle`，封装 `AVFrame*` 和 CUDA/NV12 硬件帧上下文。
- 完成`CudaPreprocessor`与Cudakernel的封装`CudaPreprocessKernels`
- 完成了`GpuDetector`的实现，用户的CUDA设备可用时会进行条件编译，如果不支持，将会直接返回fallback到CPU模式

==重要调整： 将单图的推理模式从业务层强制变为CPU模式，如果用户选择了GPU模式，则fallback到CPU进行处理并进行提示==

后续计划：
- 完成`GpuDetectionFlow`的实现，并将其接入到`DetectionSession`中，完成GPU模式的推理流程

- 完善 fallback 机制：
  - 暂时先不接入Job的drop和重建投递机制
- 调整执行模型：
  - Worker 以 Job 为最小执行单元，并负责 Session 管理。
  - 每个 Job 包含多个 Task，Worker 根据配置复用或创建 Session。
  - Job 完成后暂时保留 Session，支持后续复用。
  - Manager 负责 Job 和 Session 的状态管理。
- 修正：架构层中，单图仅支持CPU模式，不走GPU模式，故GPUPreprocessor无需注意CPU帧的处理；
- 单图GPU模式直接在业务层拦截单图GPU模式并fallback到CPU进行处理（进行提醒）
- 完善 GPU 显示流程，在 GPU 图像数据上叠加 CPU NMS 检测框和 Qt Overlay。
---

## 1. 分层

```
QML (qml/)  →  应用层 (src/app/)  →  核心层 (src/core/)  →  ONNX Runtime / OpenCV
```

这里使用三层架构： 
1. 核心层负责将输入帧进行检测并返回结果，不依赖于QML；
2. 应用层负责管理核心层的任务调度、任务提交、配置设置、结果回传给QML等，同时提供上层接口给QML；起到连接核心层和UI层的作用
3. 界面层负责将检测结果进行展示 

| 层 | 目录 | 是否依赖 Qt GUI | 是否 `QObject` |
|---|---|---|---|
| 核心层 | `src/core/` | 否（`DetectionRenderer` 用 `QImage`，属 QtGui） | 否 |
| 应用层 | `src/app/` | 是 | 是 |
| 界面层 | `qml/` | 是 | — |


---

## 2. 线程模型

整个程序目前只有两条长期存在的线程（后续将拓展为采集线程-> 工作线程 ->  UI 线程）：

1. **GUI 线程** —— 运行 QML 场景图、`DetectionApp`、`DetectionManager`。
2. **worker 线程** —— `DetectionWorker::work()` 循环，**串行**消费任务队列。

`Detector` 对象**在 worker 线程内构造并析构**，内部持有的 ONNX 会话、CUDA
context 等资源。

跨线程数据流只有两个方向，且都遵守「不共享可变状态」的原则：

- **GUI → worker**：投递 `std::unique_ptr<DetectionTask>`（经互斥量保护的队列 +
  条件变量唤醒）。任务内的 `TaskShared` 通过 `shared_ptr` 共享，其中所有会变的字段都是
  `std::atomic`，这是唯一被允许跨线程直接读写的结构。
- **worker → GUI**：Qt 信号（`taskResult` / `taskProgress` / `taskCompleted` …），
  由 Qt 自动排队到 GUI 线程。携带的 `QImage` 是隐式共享的，跨线程只是加一次引用计数。

---

## 3. 作业（Job）与任务（Task）

```
DetectionJob   一个 Job作业
 ├─ TaskShared        Job 内所有 Task 共享的可变状态（原子量），例如总体进度等
 └─ DetectionTask*    真正被 worker 执行的最小单元（unique_ptr 独占）
```

- 单图 / 单视频 / 摄像头：Job 只含 1 个 Task。
- 批量：Job 含 N 个 Task，图片与视频可混合。

**作业整体进度公式**为：

```
progress = (已结束 Task 数 + 当前 Task 内部单元比例) / Task 总数
```

任务类型以及作业类型如下：

| 类型 | 层级 | 用途 |
|---|---|---|
| `FrameSourceType` | 任务级，真正的执行原子单元 | worker 据此决定走 `runImage` 还是 `runStream`，并据此准备进度与编码器参数 |
| `DetectionJobType` | 作业级，上层的总体作业，一次作业可以分为多个任务| UI 据此决定如何呈现结果、完成时发哪个信号 |

---

## 4. 核心层模块

### 推理头 `Detector`（`Detector.h` / `Detector.cpp`）

ONNX 会话的封装，负责预处理、推理与后处理。

- 通用的C++类型，非`QObject`：错误通过 `lastError()` 上抛，警告通过 `warnings()`，结果只走返回值。
非`QObject`，无线程亲和性，不使用信号槽机制，可在任意线程使用，无需 `moveToThread`。
- `CreateSessionOptions()` 尝试启用 CUDA EP；失败时把 `cuda_ok` 置 false 并回退 CPU，
  `device()` / `usingCUDA()` 反映**实际**生效的设备。
- `alignInputSizeToModel()` 读取模型真实输入 shape 校正配置；模型为动态维（`-1`）时以
  配置尺寸为准，并生成 `DynamicInputUsed` 警告。
- `letterbox()` → `preprocess()`（BGR→RGB、归一化、HWC→NCHW）→ `runInference()`统一推理接口 →
  `parse_yolo_ouput()`解析推理输出，内部进行NMS等后处理 → `map_boxes_to_original_image()`将最后处理的检测框坐标映射回原始图像。
- `parse_yolo_ouput()` 的 `num_anchors` **由输出张量形状反推**。
- `runInference()` 内部 try/catch，在内部进行异常处理。
- 预热`warmup()` 用假输入跑几次ONNX的 `Run`，提前烧掉ONNX初始化的开销（其ONNX的Run为懒加载，在进行调用时才会进行初始化）。

### 推理所需要的各种类型 `DetectionTypes.h`

类型中枢：`LetterboxInfo`、`DetectorConfig`、`DetectionError`、`DetectorWarning`、
`DetectionResult`、`TaskShared`、`FpsCounter`、`DetectionTask`、`DetectionJob`、
`DetectionJobType`，以及 `jobTypeOf()` / `makeSingleTaskJob()` / `makeBatchJob()`。

`DetectionTask` 只持有一个 抽象类`FrameSourcePtr`指针，其中Task的执行方式、展示种类、是否累积结果（摄像头等不进行累积），全部由`source->type()`决定 。

### 策略模式帧源`FrameSource.h` / `FileFrameSource.h`

使用策略模式将帧源进行抽象。`FileFrameSource` 覆盖图片文件、视频文件、摄像头、HikCamera、Ffmpeg解码视频文件等帧源，提供统一的接口`getFrame()`获取帧。

- 构造时仅传入路径等，不进行阻塞操作，`open()`、`getFrame()`、`close()` 等操作在 worker 线程中进行，因此在GUI线程（`DetectionApp`）创建没有问题。
- 帧源内部可能不可拷贝的类型成员，例如FfmpegFrameSource内部存在FfmpegDecoder，其内部存在`std::mutex`、`std::atomic`等不可拷贝的对象，因此使用`std::unique_ptr<FrameSource>`智能指针作为统一的返回结果。

### 检测框`Box.h` / 统一的App错误 `AppError.h`

检测框`Box`为QML 可见的值类型（`Q_GADGET` + `QML_ELEMENT`），提供 `Box2Rect` / `Rect2Box` 坐标互转。

统一的App错误，`AppError`注册进QML可见类型(`Q_GADGET` + `QML_ELEMENT`)中，支持QML直接读取并且能够直接获取错误码字符串。

### 帧源`FrameSource`将图片/视频/摄像头/其他(例如海康相机、Ffmpeg解码视频帧等)转换为cv::Mat

用于替换原来的分散的文件路径/摄像头ID的获取帧的方式

更改为使用统一的抽象接口`FrameSource`作为帧源获取帧(策略模式)

`open()`打开帧源,`getFrame()`获取帧,`close()`关闭帧源

目前完成的有`FileFrameSource`和`HikCameraFrameSource`、FfmpegFrameSource

`FileFrameSource`用于为单图/视频帧/摄像头帧提供统一的接口

`HikCameraFrameSource`作为海康相机帧源,QML为其添加一个Dialog用于选择当前可用的相机设备以及选择触发模式(默认关闭并使用连续采集模式)

`FfmpegFrameSource`用于ffmpeg解码视频文件帧，目前将得到的`AVFrame*`转为`cv::Mat`，后续使用`GpuFrameSourceHandle`针对于GPU硬解进行封装，作为兼容性更强的帧源

---

## 5. 应用层模块

### `DetectionApp` —— QML 门面

把 QML 的交互转为对 `DetectionManager` 的调用，并把进度 / 结果 / 错误 / 状态 / 日志回传 QML。

- 实现 `QQmlParserStatus`：必须等 `componentComplete()`
  才能拿到 engine 注册 image provider。
- 内部只接触作业的JobId，不接触具体的`DetectionTask`、`DetectionJob`等Worker核心层的对象。
- 内部记录回看条目（`ReviewItem`）,内部保存序列号以及文件名等基础信息，降低内存使用。

### `DetectionManager` —— 作业调度

对上层的App提供「提交 / 取消 / 查进度」与作业级信号；对下把 Job 拆成 Task 投递给 worker，并把
worker 的上报汇总成 job 级事件。

- `applyConfig()` 内部完成「停旧 worker + 用新配置建新 worker」。注意到这里的ONNX会话创建后不能进行更改配置，因此只能停掉之前的会话`Session`后重新创建`Session`。
- `jobSaved` 在 `jobFinished` / `jobFailed` / `jobCancelled` **之前**发出 —— 因为 `App` 在
  终态里会清掉 `currentJobId`，晚发就可能被丢掉。
- 使用`QSet`集合`m_startedJobs` 存储当前的任务，保证一个 Job 只发一次 `jobStarted`。
- 根据Worker上报的`taskProgress`、`taskResult`、`taskCompleted`等信号，汇总成Job级别的进度`jobProgress`和完成信号`JobFinished`。
- 当前内部价仅保留一个Worker作为单线程处理+推理，后续可以进行拓展为多线程Worker进行处理。

### `DetectionWorker` —— 执行单元

在一条独立的工作线程里执行 `work()` 循环，从原子执行单元`m_queue`任务队列取 Task 执行。
内部管理`Detector`以及`QThread`线程，统一管理其生命周期。

设计：
- `setConfig()` 在 `start()` 之前调用（会话不可替换）。
- 优雅关闭设计： `stop()` 会丢弃队列中尚未开始的 Task。
-  `runImage()` 与 `runStream()`；`finishUnit()` 负责单帧的「渲染 → 计数 → 推送」。
- `failTaskIfUnfinished()` 在任务抛异常时进行统一的补发失败信号，避免重复发错误信号导致覆盖。

### 渲染器`DetectionRenderer`

把「帧 + 检测框」渲染成可直接给 QML 的 `QImage`。

渲染**必须**放在 worker 线程：`cv::Mat` **浅拷贝**共享像素，跨线程传 `Mat` 等于传一块会被下一帧
覆写的内存。在 worker 线程内一次性画好框并转成 `QImage`，上层拿到的是一份独立、可安全
显示的内存数据。

- `renderMat()` 返回 `cv::Mat`（保存视频时必须用这个）；
- `render()` 在 `renderMat()` 基础上转 `QImage`（显示路径用这个）。

### 检测结果保存器`DetectionResultSaver`

保存目录设置：`选择的根目录/detection_results/`，图片写 `原文件名.jpg`，视频/摄像头写
`原文件名.mp4`。仅保留最后的检测结果（画框的图片、视频）。

无锁：只在 worker 线程内使用，不跨线程。
保存失败不会中断，但会记录错误信息：记录在`lastError()` 里，向调用方上报一次。

### QML图像提供器`DetectionImageProvider`

既服务「实时画面」（`image://detection/latest?rev=N`），也服务「结果回看」
（`image://detection/frame/<seq>`）。

回看缓冲是**有界环形**（`kMaxReviewFrames = 60`）。

### 日志`Logger`

Qt 日志器：通过 `qInstallMessageHandler` 接管 Qt 自身的 `qDebug` / `qInfo` /
`qWarning` / `qCritical`，统一汇入本日志器，再以 `messageLogged` 信号推给 QML 日志面板。

---

## 6. 界面层QML

| 文件 | 职责 |
|---|---|
| `main.qml` | 主窗口：顶部标题栏、警告/成功横幅、左侧常驻配置栏 + 结果输出 + 状态栏、右侧工具栏 + 结果区 + 日志区 |
| `Theme.qml` | 配色单例，跟随系统深浅色 |
| `LogView.qml` | 日志列表，按级别着色 |
| `ConfigDialog.qml` | 模型与检测参数表单，确定后调用 `app.setConfig(...)` |
| `CameraDialog.qml` | 摄像头设备选择，确定后调用 `app.onDetectCameraClicked(id)` |
|`HikCameraDialog.qml`| 海康相机设备选择，确定后调用 `app.onDetectHikCameraClicked(serial,triggerMode)` |
结果画面用 `Image` 绑定 `app.frameSource()`：

- `source` 必须带上会变化的 `rev`，否则 QML 会因为 URL 完全相同而吃缓存；
- `cache` 必须为 `false`，否则每个 `rev` 都会在图片缓存里留一份，内存只涨不降；
- `asynchronous` 必须为 `false`，否则异步取图会乱序，画面会跳帧。

检测开始后主动暂停原始视频预览（`mediaPlayer.pause()`），避免两路视频同时解码。

---

## 7. 扩展点

- **GPU 预处理**：`CudaPreprocessor` 仅做必要的 tensor 布局转换，尽量调用 FFmpeg CUDA filter、CUDA/NPP 等 API；
- **FrameHandle**统一的帧句柄，`CpuFrameHandle`持有独立的`cv::Mat`，`GpuFrameHandle`持有`AVFrame*`和CUDA硬件帧上下文；
- **Processor**预处理，`CpuPreprocessor`和`CudaPreprocessor`分别处理CPU和GPU的预处理，统一接口为`preprocess()`，返回对应的tensor；
- 分离流程设计：使用CUDA时：
GPU预处理+GPU推理， 推理输出检测框在CPU NMS，最后将NMS后的检测框使用QT的Overlay叠加在GPU的内存图像上，最后使用QT的GPU显示；
使用CPU时：CPU预处理+CPU推理+CPU解析+CPU NMS；
提供fallback机制，设计为：在GPU预处理或GPU推理失败时，释放GPU资源，整体切换到CPU预处理+CPU推理+CPU解析+CPU NMS流程；
- 设置采集线程->工作线程->UI线程的多线程处理模式，采集线程负责获取帧源，内部使用有界队列进行缓存帧（防止解码过快无限增长内存），
工作线程负责预处理+推理+解析+NMS，UI线程负责显示和交互；
- **多 worker 并行**：`DetectionManager::m_worker` 目前是单个 `unique_ptr`，注释已注明
  「单路输入先用 1 个 worker」。改为 `std::vector` 并在 `dispatch()` 中轮询/按负载分配即可，
  两级 Job/Task 模型本身已支持多 worker 消费同一个 Job 的多个 Task。
