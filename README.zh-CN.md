# YOLO —— 实时目标检测桌面应用

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Qt 6](https://img.shields.io/badge/Qt-6.11-41CD52.svg)](https://www.qt.io/)
[![ONNX Runtime](https://img.shields.io/badge/ONNX%20Runtime-1.30-005CED.svg)](https://onnxruntime.ai/)
[![OpenCV](https://img.shields.io/badge/OpenCV-5.0-5C3EE8.svg)](https://opencv.org/)

基于 **Qt 6（Quick/QML）**、**ONNX Runtime** 与 **OpenCV** 的实时 YOLO 目标检测桌面应用。
支持对**图片、视频文件、摄像头实时流**做推理，在画面上绘制检测框，并可将标注后的结果
落盘保存。

界面使用 QML 编写，窗口标题为「检测头」。文档提供中英双语：
英文见 [README.md](README.md)，本篇为简体中文说明。

---

## 目录

- [功能特性](#功能特性)
- [系统架构](#系统架构)
- [目录结构](#目录结构)
- [环境依赖](#环境依赖)
- [编译构建](#编译构建)
- [使用说明](#使用说明)
- [配置说明](#配置说明)
- [模型说明](#模型说明)
- [结果输出](#结果输出)
- [常见问题](#常见问题)
- [许可证](#许可证)

---

## 功能特性

**推理能力**

- 支持任意 YOLO 系 ONNX 检测模型（输出形如 `[1, 4 + 类别数, 候选框数]`）。
- CUDA 执行提供器，且在 CUDA / cuDNN 不可用时**自动回退 CPU**。
- 正确的 `letterbox` 预处理，以及把预测框从填充空间映射回原图坐标。
- 候选框数量由输出张量实际形状反推，因此**非 640 输入同样正确**
  （640 → 8400，320 → 2100，1280 → 33600）。
- 启动时预热（warm-up），提前烧掉 CUDA context、cuDNN handle、内存 arena 首次分配等
  一次性开销，避免第一帧检测卡顿。
- 输入尺寸自动对齐：当配置尺寸与模型真实输入不符时自动按模型校正，并通过横幅
  **显式告知用户**，而不是让界面显示值与实际使用值悄悄分叉。

**输入与作业**

- 单图、单视频、摄像头实时流，以及**批量作业**（图片与视频可混排）。
- 两级作业模型：一个 *Job* 对应界面上的一个进度条，内部拆成 N 个 *Task* 交由专用工作
  线程执行。
- 统一的进度公式，同时覆盖批量作业与单流逐帧进度。
- 长作业（如摄像头流）可随时取消，工作线程逐单元检查取消标志。

**界面**

- 实时标注预览，画面左上角叠加帧率 / 推理耗时。
- **结果回看不阻塞推理**：用「上一个 / 下一个 / 回到实时」翻阅历史帧时，推理照常全速
  运行（有界环形缓冲，不对工作线程形成背压）。
- 进度条支持「总量未知」（摄像头流）的不确定态。
- 配置对话框：模型路径、类别名、置信度 / NMS 阈值、线程数、CUDA 设备号、输入尺寸、
  CUDA 开关。
- 「保存推理结果」开关 + 输出目录选择。
- 内置日志面板（按级别着色），侧边栏状态栏与日志同源。
- 深色 / 浅色主题跟随系统，并支持运行时实时切换。

---

## 系统架构

项目刻意做了分层：推理内核**不依赖界面、也不是 `QObject`**，而 QML 层不接触线程与
检测器内部细节。

```
                      ┌──────────────────────────────────────────┐
   QML 层             │  main.qml · Theme · LogView ·            │
   (qml/)             │  ConfigDialog · CameraDialog             │
                      └───────────────┬──────────────────────────┘
                                      │  属性绑定 / Q_INVOKABLE
                      ┌───────────────▼──────────────────────────┐
   应用层             │  DetectionApp      (QML 门面)            │
   (src/app/)         │  DetectionManager  (作业调度)            │
                      │  DetectionWorker   (工作线程)            │
                      │  DetectionRenderer (帧 → QImage)         │
                      │  DetectionResultSaver (结果落盘)         │
                      │  DetectionImageProvider (image:// 提供器)│
                      │  Logger            (日志桥接)            │
                      └───────────────┬──────────────────────────┘
                                      │  DetectorConfig / DetectionResult
                      ┌───────────────▼──────────────────────────┐
   核心层             │  Detector   (ONNX 会话、预处理、后处理)  │
   (src/core/)        │  DetectionTypes · Box · AppError         │
                      └───────────────┬──────────────────────────┘
                                      │
                      ┌───────────────▼──────────────────────────┐
                      │  ONNX Runtime (CUDA / CPU)  ·  OpenCV    │
                      └──────────────────────────────────────────┘
```

几个关键设计取舍（更详细的说明见 [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)）：

- **`Detector` 不是 `QObject`。** 错误通过 `lastError()` / `warnings()` 上抛，结果只走
  返回值通道，因此可以在任意线程使用，无需 `moveToThread`。
- **渲染放在工作线程。** `cv::Mat` 是浅拷贝共享像素的，把 `Mat` 直接交给 GUI 线程会与下一帧
  产生竞争；工作线程改为转成隐式共享的 `QImage`。
- **回看缓冲绝不阻塞推理。** UI 只按自己的节奏推进索引，跨线程不存在任何「等待 / 放行」
  标志位。
- **`DetectionApp` 只认不透明的 `jobId`。** 线程、队列、取消这些事全部收敛在
  `DetectionManager` / `DetectionWorker` 内部。

---

## 目录结构

```
.
├── CMakeLists.txt              # 顶层构建脚本（目标、QML 模块、DLL 部署）
├── CMakePresets.json           # Qt 生成的 CMake 预设
├── LICENSE                     # MIT 许可证
├── README.md                   # 英文说明
├── README.zh-CN.md             # 中文说明（本文件）
├── .gitignore
│
├── cmake/
│   └── qt.cmake                # Qt5 / Qt6 兼容垫片
│
├── src/                        # C++ 源码
│   ├── main.cpp                # 入口：主题调色板、日志接管、QML 引擎启动
│   ├── core/                   # 推理内核 —— 无界面、无 QObject
│   │   ├── Detector.{h,cpp}        # ONNX 会话、letterbox、YOLO 解码、NMS、视频循环
│   │   ├── DetectionTypes.h        # DetectorConfig、错误/警告、Job/Task/Box 模型
│   │   ├── Box.h                   # 检测框值类型（Q_GADGET，可被 QML 读取）
│   │   └── AppError.h              # 应用层错误值类型
│   ├── app/                    # Qt 应用层
│   │   ├── DetectionApp.{h,cpp}        # QML 门面：属性、Q_INVOKABLE 槽、横幅提示
│   │   ├── DetectionManager.{h,cpp}    # 作业调度：提交/取消，汇总 Task 事件
│   │   ├── DetectionWorker.{h,cpp}     # 工作线程 + 任务队列
│   │   ├── DetectionRenderer.{h,cpp}   # 画框/标签/帧率，Mat → QImage
│   │   ├── DetectionResultSaver.{h,cpp}# 输出标注后的 .jpg / .mp4
│   │   ├── DetectionImageProvider.{h,cpp} # image://detection 提供器 + 回看环形缓冲
│   │   └── Logger.{h,cpp}              # QML 单例日志器，桥接 qDebug/qWarning
│   └── tools/
│       └── ImageShower.{h,cpp}     # 独立的 OpenCV imshow 预览工具（调试用）
│
├── qml/                        # QML 界面
│   ├── main.qml                # 主窗口：工具栏、侧边栏、结果区、日志分隔区
│   ├── Theme.qml               # 配色 / 主题单例
│   ├── LogView.qml             # 日志列表，按级别着色
│   ├── ConfigDialog.qml        # 模型与检测参数对话框
│   └── CameraDialog.qml        # 摄像头设备选择
│
├── resources/                  # 通过 Qt 资源系统编译进二进制的素材
│   ├── images.qrc              # 资源清单
│   ├── images/                 # 示例图片，内嵌为 :/image1
│   └── models/                 # 示例 ONNX 模型，内嵌为 :/models/african-wildlifeModel
│
├── models/                     # 你自己的模型权重（被 git 忽略，不入库）
├── detection_results/          # 标注结果默认输出目录（被 git 忽略）
└── docs/
    └── ARCHITECTURE.md         # 逐模块的详细设计说明
```

> `models/` 与 `detection_results/` 刻意不入版本控制，见 [`.gitignore`](.gitignore)。
> 保留这两个目录是为了让默认路径开箱可用。

---

## 环境依赖

主要验证平台为 **Windows 10/11 x64**。构建脚本本身是可移植的，但当前包含 Windows 专有的
运行时 DLL 部署逻辑。

| 依赖 | 使用版本 | 说明 |
|---|---|---|
| 编译器 | MSVC 2022 (19.51) | 需要 C++20 |
| CMake | ≥ 3.16 | |
| Qt | 6.11.1 | 需要 `Core`、`Quick`、`Multimedia` 模块 |
| OpenCV | 5.0.0 | `opencv_world` 动态库版本 |
| ONNX Runtime | 1.30.0 | GPU/CUDA 13 包；CPU 包同样可用 |
| 生成器 | Ninja（或 NMake/JOM） | |
| CUDA Toolkit + cuDNN | 13.x / 9.x | *可选* —— 仅 GPU 推理需要 |

GPU 推理是可选路径：若缺少 CUDA / cuDNN 运行库，检测器会记录一条警告并自动回退到 CPU
执行提供器。

---

## 编译构建

1. 克隆仓库：

   ```bash
   git clone https://github.com/<你的账号>/<你的仓库>.git
   cd <你的仓库>
   ```

2. 配置。把 CMake 指向你本机的 Qt、OpenCV 与 ONNX Runtime：

   ```bash
   cmake -S . -B build -G Ninja ^
     -DCMAKE_BUILD_TYPE=Release ^
     -DCMAKE_PREFIX_PATH="D:/Qt/6.11.1/msvc2022_64" ^
     -DOpenCV_DIR="D:/opencv/build" ^
     -Donnxruntime_DIR="D:/onnxruntime-win-x64-gpu_cuda13-1.30.0/lib/cmake/onnxruntime"
   ```

   > Linux / macOS 下把 `^` 换成 `\`。`CMAKE_PREFIX_PATH` 指向 Qt 的 `lib/cmake` 所在
   > 目录；`OpenCV_DIR` 指向包含 `OpenCVConfig.cmake` 的目录。

3. 编译：

   ```bash
   cmake --build build --parallel
   ```

4. 运行。可执行文件输出在构建目录下，所需的运行时 DLL（OpenCV、ONNX Runtime、
   CUDA/cuDNN）会自动复制到其旁边：

   ```
   build/YOLO.exe
   ```

### 构建选项

| 选项 | 默认值 | 说明 |
|---|---|---|
| `DEPLOY_CUDA_RUNTIME` | `ON` | 是否把 CUDA/cuDNN 运行库复制到可执行文件旁。若已通过 `PATH` 提供，可用 `-DDEPLOY_CUDA_RUNTIME=OFF` 关闭。 |

---

## 使用说明

启动 `YOLO.exe` 后：

1. **配置模型** —— 点击 **修改配置**，选择 `.onnx` 模型文件，填写类别名（每行一个或用逗号
   分隔），设置阈值与输入尺寸，点 **确定** 生效。

   > 提交有效配置后即开始准备推理；更换模型或阈值会重建 ONNX 会话。

2. **选择输入**

   | 按钮 | 作用 |
   |---|---|
   | **选择文件 ▾** | 选择单张图片或单个视频 |
   | **摄像头检测** | 选择设备号并启动实时流 |
   | **批量检测** | 多选图片 / 视频，作为一个作业依次跑完 |
   | **停止检测** | 取消当前作业（仅作业运行中可用） |

3. **查看结果** —— 标注后的画面显示在结果区。用 **上一个** / **下一个** 翻阅缓冲结果，
   用 **回到实时** 恢复跟随实时画面。回看过程不会暂停推理。

4. **保存结果（可选）** —— 勾选 **保存推理结果** 并选择目录。标注图片写为 `.jpg`，
   视频 / 摄像头流写为 `.mp4`，统一放在 `<目录>/detection_results/` 下。

底部日志面板按级别着色显示全部日志，侧边栏状态框与日志同源，展示最近一条。

---

## 配置说明

检测参数在运行时通过 **修改配置** 对话框设置：

| 参数 | 含义 |
|---|---|
| 模型路径 | `.onnx` 模型文件路径 |
| 类别名 | 每行一个（或逗号分隔）；顺序必须与模型输出一致 |
| 置信度阈值 | 保留检测框的最低分数（0.01–0.99） |
| NMS 阈值 | 非极大值抑制的 IoU 阈值 |
| 线程数 | 算子内部线程数（`0` 表示使用 ORT 默认值） |
| CUDA 设备号 | CUDA 执行提供器使用的 GPU 序号 |
| 输入宽 / 高 | 网络输入尺寸；与模型不符时会自动校正 |
| 启用 CUDA | 尝试启用 CUDA EP；不可用时自动回退 CPU |

**示例模型路径**硬编码在 `src/main.cpp`（`AfricanWildlife_model_path`、`People_model_path`），
指向作者本机的训练产物，仅供已注释掉的演示函数使用 —— 请替换或删除。

---

## 模型说明

任何导出为 ONNX 的 YOLO 检测模型都应当可用，前提是：

- 输入为单个 `float32` 张量，形状 `[1, 3, H, W]`（支持动态 `H`/`W`）；
- 输出为 `[1, 4 + 类别数, 候选框数]`，坐标 `(cx, cy, w, h)` 位于 letterbox 空间。

填写的类别名顺序必须与模型训练数据完全一致。

`resources/images.qrc` 内嵌了两份示例素材：

- `:/image1` —— 示例图片；
- `:/models/african-wildlifeModel` —— 一个小型的非洲野生动物检测模型。

它们只是方便试用应用的默认素材；实际推理所用模型来自配置对话框。

---

## 结果输出

开启 **保存推理结果** 后，结果写入：

```
<所选目录>/
└── detection_results/
    ├── <原文件名>.jpg     # 图片任务
    └── <原文件名>.mp4     # 视频 / 摄像头任务
```

只保存「画了框的画面」，不写 labels —— 用途是视觉查看，而非训练或评估。保存失败时错误
会上报一次，但**检测会继续**，不会中断。

---

## 常见问题

| 现象 | 可能原因 / 处理 |
|---|---|
| 配置阶段报 `Could NOT find OpenCV` | `OpenCV_DIR` 指向了用其它工具链（如 MinGW）构建的包。请指向包含 `x64/vc16/lib` 的 MSVC 构建。 |
| 程序启动失败、缺少 `*.dll` | 运行时 DLL 未部署。确认构建后复制步骤已执行，或把 OpenCV / ONNX Runtime 的 `bin` 目录加入 `PATH`。 |
| 检测框位置偏移或缩放不对 | 输入尺寸与模型不匹配。查看警告横幅 —— 应用会自动对齐并提示改动。 |
| 没有用上 CUDA | 未找到 CUDA / cuDNN 运行库。设置 `CUDA_PATH`/`CUDNN_PATH`（或 `PATH`）以便 `DEPLOY_CUDA_RUNTIME` 找到它们；否则会使用 CPU EP。 |
| 首次推理很慢 | 属正常现象 —— 启动时已预热会话，但换模型后的第一帧仍需付出会话创建成本。 |

---

## 许可证

本项目基于 [MIT 许可证](LICENSE) 发布。

第三方组件各自遵循其原有许可证：

- **Qt** —— LGPLv3 / 商业许可（详见你的 Qt 安装）；
- **OpenCV** —— Apache-2.0；
- **ONNX Runtime** —— MIT。
