# YOLO — Real-Time Object Detection Desktop App

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Qt 6](https://img.shields.io/badge/Qt-6.11-41CD52.svg)](https://www.qt.io/)
[![ONNX Runtime](https://img.shields.io/badge/ONNX%20Runtime-1.30-005CED.svg)](https://onnxruntime.ai/)
[![OpenCV](https://img.shields.io/badge/OpenCV-5.0-5C3EE8.svg)](https://opencv.org/)

A cross-platform desktop application for **real-time YOLO object detection**, built with
**Qt 6 (Quick/QML)**, **ONNX Runtime** and **OpenCV**. It runs inference on images, video
files, USB camera streams and Hikvision (MVS) industrial cameras, draws bounding boxes on the
frames, and optionally writes annotated results to disk.

The user interface is written in QML and currently ships with **Chinese labels**
(the app window is titled *检测头*). The code base, comments and this README are bilingual:
English here, [简体中文说明请见 README.zh-CN.md](README.zh-CN.md).

---

## Table of Contents

- [Features](#features)
- [Architecture](#architecture)
- [Technology stack](#technology-stack)
- [Planned updates](#planned-updates)
- [Directory Structure](#directory-structure)
- [Requirements](#requirements)
- [Build](#build)
- [Usage](#usage)
- [Configuration](#configuration)
- [Models](#models)
- [Output](#output)
- [Troubleshooting](#troubleshooting)
- [License](#license)

---

## Features

**Inference**

- Runs any YOLO-style ONNX model (detection head producing `[1, 4 + num_classes, num_anchors]`).
- CUDA execution provider with **automatic CPU fallback** when CUDA/cuDNN is unavailable.
- Correct `letterbox` preprocessing and de-letterbox mapping back to original image coordinates.
- Anchor count is derived from the actual output tensor shape, so **non-640 input sizes work**
  (640 → 8400 anchors, 320 → 2100, 1280 → 33600).
- Model warm-up on start-up to hide first-inference latency (CUDA context, cuDNN handle,
  arena allocation).
- Automatic input-size alignment: if the configured size disagrees with the model's real
  input shape, the app corrects it and **tells the user** via a warning banner instead of
  silently diverging.

**Inputs & jobs**

- Single image, single video, USB camera, Hikvision (MVS) industrial camera, and **batch**
  (mixed images + videos in one job).
- **Pluggable frame sources** (strategy pattern). Every input is a `FrameSource` exposing
  `open()` / `getFrame()` / `close()`; the worker drives any task through those three calls
  and picks the run path (`runImage` vs `runStream`) from `type()`, while the job factory
  derives the job type from the same value. New source types plug in at the app-side factory —
  no new `submitXxx()` entry point, no change to the scheduler or the UI.
- Hikvision support covers GigE and USB3 industrial cameras, discovered by serial number, with
  a selectable trigger mode: continuous (default), software trigger, or hardware trigger on
  Line0 / Line1 / Line2.
- Two-level job/task model: one *Job* per UI progress bar, split into N *Tasks* executed by a
  dedicated worker thread.
- Unified progress formula covering both batch and per-frame progress.
- Cancellable long-running jobs (e.g. camera streams) via a per-unit cancel flag.

**UI**

- Live annotated preview, with an FPS/duration overlay drawn on the frame.
- **Non-blocking result review**: browse previous frames with *Previous / Next / Follow live*
  while inference keeps running at full speed (bounded ring buffer, no back-pressure on the
  worker thread).
- Progress bar with an indeterminate mode for streams of unknown length.
- Configuration dialog: model path, class names, confidence/NMS thresholds, thread count,
  CUDA device id, input size, CUDA on/off.
- "Save results" switch + output-folder picker.
- Built-in log panel with severity colouring, plus a status bar fed from the same log stream.
- Dark / light theme that follows the system colour scheme and switches live.

---

## Architecture

The application is split into three layers. QML owns the screen and user actions. The app layer turns those actions into jobs and keeps the worker lifecycle out of the UI. The core layer reads frames, runs the selected CPU or GPU flow, and returns detection results without depending on QML or `QObject`.

[View the generated architecture diagram](docs/architecture.html)

The current execution model is built around a `DetectionJob`. The manager creates a job from the input description, the worker executes that job, and the session created for the job keeps the matching frame sources and detection flow together. A job can contain several tasks, which is what lets a mixed batch of images and videos use one progress bar while each task keeps its own source.

```text
QML
  -> DetectionApp
  -> DetectionManager
  -> DetectionWorker
  -> DetectionSession
  -> FrameSource + DetectionFlow
  -> DetectionManager
  -> DetectionApp
  -> QML
```

### Core modules

- `DetectionApp` is the QML-facing facade. It exposes configuration, commands, progress, warnings, logs, and result images. It does not create frame sources or touch worker threads.
- `DetectionManager` creates and tracks jobs, collects task results, handles cancellation, and owns the fallback policy reported to the UI.
- `DetectionWorker` runs jobs on the worker thread. It creates the session for a job and executes the tasks in that job without making the QML layer wait on inference.
- `DetectionSession` stores the job-level mode and configuration. It creates the matching frame source and keeps a `CpuDetectionFlow` or `GpuDetectionFlow` alive for the tasks that use it.
- `FrameSource` hides where frames come from. `ImageFrameSource` reads single images, `FfmpegFrameSource` handles videos and ordinary camera devices, and `HikCameraFrameSource` keeps the Hikvision SDK details behind its own boundary.
- `FrameHandle` hides how a frame is stored. CPU BGR frames use `CpuFrameHandle`; decoded CUDA/NV12 frames and uploaded image frames use GPU handle types. The flow receives a handle instead of depending directly on one decoder or one image library.
- `DetectionFlow` keeps preprocessing, inference, and result preparation together. The CPU flow uses OpenCV and ONNX Runtime CPU. The GPU flow uses CUDA preprocessing, ONNX Runtime CUDA, and CPU NMS where the current path still needs it.
- `DetectionRenderer`, `DetectionImageProvider`, and `DetectionResultSaver` keep display, review, and file output at the app boundary. The worker converts results into data that QML can consume instead of exposing `cv::Mat` or SDK objects to the UI thread.

### Technology stack

| Area | Technology | Use in this project |
|---|---|---|
| Desktop UI | Qt 6, Qt Quick, QML | Window, dialogs, themes, logs, live result review |
| Application code | C++20, Qt Core, Qt Multimedia | Job submission, worker thread, signals, configuration, device selection |
| Model runtime | ONNX Runtime 1.30 | CPU and CUDA execution providers for YOLO ONNX models |
| Image processing | OpenCV 5 | Image loading, CPU preprocessing, image conversion, drawing helpers |
| Video and camera input | FFmpeg | Video decoding, camera devices, software and CUDA decode paths |
| GPU processing | CUDA 13, cuDNN 9, custom CUDA kernels | Device setup, GPU frame storage, BGR/NV12 preprocessing, GPU inference |
| Industrial cameras | Hikvision MVS SDK | GigE and USB3 camera discovery, trigger modes, frame acquisition |
| Build and packaging | CMake, Ninja, Qt Resource System | Build configuration, QML resources, runtime DLL deployment |

### Current data paths

The CPU path accepts CPU BGR frames, runs OpenCV preprocessing, invokes the ONNX Runtime CPU provider, and completes postprocessing before the result reaches the renderer.

The GPU path has two input paths. FFmpeg can provide GPU NV12 frames for the CUDA preprocessing kernel. Single images arrive as CPU BGR frames, then the session uploads them to the GPU and uses the BGR CUDA preprocessing path. If the CUDA device, GPU session, or upload path cannot be created, the manager reports the failure and rebuilds the session in CPU mode. A normal input error only fails the current task.

The UI review buffer is bounded and does not pause inference. The worker publishes reviewable images, while QML moves through the buffer with Previous, Next, and Follow live. Saving results remains an app-layer concern, so the core inference code does not need to know where files will be written.

For the detailed module notes and thread decisions, see [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) and the [architecture diagram](docs/architecture.html).

### Planned updates

The next changes follow the same direction already recorded in `docs/ARCHITECTURE.md` and in the open issues of the repository:

1. **Complete the GPU input contract.** Validate CPU BGR24 and GPU NV12 from the actual frame layout, including plane pointers, stride, dimensions, and color range. Unsupported formats should fail the current task with a clear error.
2. **Finish the single-image GPU path.** Keep CPU BGR upload, buffer reuse, BGR CUDA preprocessing, GPU inference, CPU NMS, and Qt overlay in one working path. A failed upload should trigger CPU session recovery instead of forcing every single image to CPU in advance.
3. **Move the worker queue to jobs.** The worker should receive a `DetectionJob`, create or reuse a session for that job, execute its tasks, and keep session lifetime separate from individual task lifetime. The manager will remain responsible for job and session state.
4. **Add performance monitoring.** Issue [#14](https://github.com/fanyu99/YOLO-Realtime-Detector/issues/14) proposes an injected monitor with a real implementation for upload, letterbox, detection, and NMS timings, plus an empty implementation that keeps the same RAII timing boundary for normal runs. The result should appear in a detachable non-modal panel that polls at a fixed interval.
5. **Split acquisition, inference, and display when measurements justify it.** Issue [#1](https://github.com/fanyu99/YOLO-Realtime-Detector/issues/1) records the next performance direction: an FFmpeg CUDA acquisition thread, a bounded frame queue, an inference worker, and a UI rendering path. The project will keep the current serial path until the measured bottleneck and the handoff cost are clear.
6. **Validate before adding more decode paths.** Test GPU single-image inference, mixed image/video batches, broken images, and CPU/GPU preprocessing consistency first. Only then add an FFmpeg image hardware decode path.

The completed work behind the current design is tracked in the closed issues for the GPU inference chain, unified frame handles, FFmpeg input integration, resource-based sample assets, and the session-based execution model. The architecture document keeps the implementation details and the reasons for each change.

---

## Directory Structure

```
.
├── CMakeLists.txt              # Top-level build script (targets, QML module, DLL deployment)
├── CMakePresets.json           # Qt-generated CMake presets
├── LICENSE                     # MIT license
├── README.md                   # This file (English)
├── README.zh-CN.md             # Chinese README
├── .gitignore
│
├── cmake/
│   └── qt.cmake                # Qt5/Qt6 compatibility shims
│
├── src/                        # C++ sources
│   ├── main.cpp                # Entry point: theme palette, logging, QML engine bootstrap
│   ├── core/                   # Inference engine — no UI, no QObject
│   │   ├── Detector.{h,cpp}        # ONNX session, letterbox, YOLO decode, NMS, video loops
│   │   ├── DetectionTypes.h        # DetectorConfig, errors/warnings, Job/Task/Box models
│   │   ├── FrameSource.h           # Frame-source interface (strategy) + path/type helpers
│   │   ├── FileFrameSource.{h,cpp}      # Image / video file / USB camera source
│   │   ├── HikCameraFrameSource.{h,cpp} # Hikvision MVS source: enumeration, trigger modes
│   │   ├── Box.h                   # Bounding-box value type (Q_GADGET, QML-exposed)
│   │   └── AppError.h              # Application-level error value type
│   └── app/                    # Qt application layer
│       ├── DetectionApp.{h,cpp}        # QML façade: properties, Q_INVOKABLE slots, banners
│       ├── DetectionManager.{h,cpp}    # Job scheduler: submits/cancels, aggregates task events
│       ├── DetectionWorker.{h,cpp}     # Worker thread + task queue
│       ├── DetectionRenderer.{h,cpp}   # Draws boxes/labels/FPS, Mat → QImage
│       ├── DetectionResultSaver.{h,cpp}# Writes annotated .jpg / .mp4 results
│       ├── DetectionImageProvider.{h,cpp} # image://detection provider + review ring buffer
│       └── Logger.{h,cpp}              # QML singleton log sink, bridges qDebug/qWarning
│
├── qml/                        # QML user interface
│   ├── main.qml                # Main window: toolbar, sidebar, result view, log split
│   ├── Theme.qml               # Colour palette / theme singleton
│   ├── LogView.qml             # Log list with per-level colouring
│   ├── ConfigDialog.qml        # Model & detection parameter dialog
│   ├── CameraDialog.qml        # Camera device picker
│   └── HikCameraDialog.qml     # Hikvision device picker + trigger mode
│
├── resources/                  # Assets compiled into the binary via Qt Resource System
│   ├── images.qrc              # Resource manifest
│   ├── images/                 # Sample image bundled as :/image1
│   └── models/                 # Sample ONNX model bundled as :/models/african-wildlifeModel
│
├── models/                     # Your own model weights (git-ignored, not committed)
├── detection_results/          # Default output folder for annotated results (git-ignored)
└── docs/
    └── ARCHITECTURE.md         # Detailed module-by-module design notes
```

> `models/` and `detection_results/` are intentionally kept out of version control — see
> [`.gitignore`](.gitignore). They exist so that the default paths work out of the box.

---

## Requirements

Tested on **Windows 10/11 x64**. The build system is portable, but the current CMake script
includes Windows-specific runtime DLL deployment.

| Dependency | Version used | Notes |
|---|---|---|
| Compiler | MSVC 2022 (19.51) | C++20 required |
| CMake | ≥ 3.16 | |
| Qt | 6.11.1 | Modules: `Core`, `Quick`, `Multimedia` |
| OpenCV | 5.0.0 | `opencv_world` shared build |
| ONNX Runtime | 1.30.0 | GPU/CUDA 13 package; CPU package works too |
| Generator | Ninja (or NMake/JOM) | |
| CUDA Toolkit + cuDNN | 13.x / 9.x | *Optional* — only for GPU inference |
| Hikvision MVS SDK | — | **Required to build** — provides `MvCameraControl.h` / `MvCameraControl.lib`. Its root is read from `%MVCAM_COMMON_RUNENV%`, falling back to `D:/MVS/Development`. |

The GPU path is optional: if CUDA/cuDNN DLLs are missing the detector logs a warning and
falls back to the CPU execution provider automatically.

The Hikvision path is **not** optional at build time: `HikCameraFrameSource` links
`MvCameraControl.lib`, so the matching MVS **runtime** must be loadable at start-up (the exe
import table references `MvCameraControl.dll` and its GenICam / transport layers) even if you
never open a Hik camera. `DEPLOY_MVS_RUNTIME` — see below — takes care of that.

---

## Build

1. Clone the repository:

   ```bash
   git clone https://github.com/<your-account>/<your-repo>.git
   cd <your-repo>
   ```

2. Configure. Point CMake at your Qt, OpenCV and ONNX Runtime installations:

   ```bash
   cmake -S . -B build -G Ninja ^
     -DCMAKE_BUILD_TYPE=Release ^
     -DCMAKE_PREFIX_PATH="D:/Qt/6.11.1/msvc2022_64" ^
     -DOpenCV_DIR="D:/opencv/build" ^
     -Donnxruntime_DIR="D:/onnxruntime-win-x64-gpu_cuda13-1.30.0/lib/cmake/onnxruntime"
   ```

   > On Linux/macOS replace `^` with `\`. `CMAKE_PREFIX_PATH` is where Qt's `lib/cmake` lives;
   > `OpenCV_DIR` is the folder containing `OpenCVConfig.cmake`.
   >
   > The Hikvision MVS SDK is located through the `MVCAM_COMMON_RUNENV` environment variable
   > (set by the MVS installer). Without it, CMake falls back to `D:/MVS/Development`; if your
   > SDK lives elsewhere, either export that variable or edit `MVS_SDK_ROOT` at the top of
   > `CMakeLists.txt`. Configuration fails outright if `MvCameraControl` cannot be found.

3. Build:

   ```bash
   cmake --build build --parallel
   ```

4. Run. The executable is written to the build directory, with all required runtime DLLs
   (OpenCV, ONNX Runtime, CUDA/cuDNN) copied next to it:

   ```
   build/YOLO.exe
   ```

### Build options

| Option | Default | Description |
|---|---|---|
| `DEPLOY_CUDA_RUNTIME` | `ON` | Copy CUDA/cuDNN runtime DLLs next to the executable. Turn off (`-DDEPLOY_CUDA_RUNTIME=OFF`) if you already provide them through `PATH`. |
| `DEPLOY_MVS_RUNTIME` | `ON` | Copy the Hikvision MVS runtime (`MvCameraControl.dll` plus the GenICam / transport DLLs, ~110 MB) next to the executable. Auto-detected from the SDK root and the usual `MVS/Runtime/Win64_x64` locations; set `-DMVS_RUNTIME_DIR=<dir>` for a non-standard install, or `-DDEPLOY_MVS_RUNTIME=OFF` to rely on `PATH`. |

---

## Usage

Launch `YOLO.exe`, then:

1. **Configure a model** — click **修改配置** (*Edit configuration*) and pick your `.onnx` file,
   enter the class names (one per line or comma-separated), and set the thresholds and input
   size. Click **确定** to apply.

   > Inference starts as soon as a valid configuration is submitted; changing the model or
   > thresholds rebuilds the ONNX session.

2. **Pick an input**

   | Button | Action |
   |---|---|
   | **选择文件 ▾** | Choose a single image or video |
   | **摄像头检测** | Pick a camera device id and start a live stream |
   | **海康相机检测** | Enumerate Hikvision industrial cameras, pick one by serial number, choose a trigger mode, and start the live stream |
   | **批量检测** | Multi-select images and/or videos to run as one job |
   | **停止检测** | Cancel the running job (enabled only while busy) |

   > **海康相机检测** is a separate path from **摄像头检测**: the first talks to GigE / USB3
   > industrial cameras through the Hikvision MVS SDK, the second through OpenCV's device
   > index. Both end up as the same kind of job, so progress, review, cancellation and result
   > saving behave identically.
   >
   > Trigger mode: *off* — continuous acquisition, the default — *software*, or *line0* /
   > *line1* / *line2* for a hardware trigger. An unrecognised value logs a warning and falls
   > back to *off*. Modes the device does not support surface as an error in the log when the
   > stream starts.

3. **Watch the result** — annotated frames appear in the result view. Use **上一个** /
   **下一个** to step through buffered results and **回到实时** to resume following the live
   frame. Inference is never paused by reviewing.

4. **Save output (optional)** — tick **保存推理结果** and choose a folder. Annotated images are
   written as `.jpg`, videos/camera streams as `.mp4`, under `<folder>/detection_results/`.

The log panel at the bottom shows everything with severity colouring; the sidebar status box
mirrors the most recent entry.

---

## Configuration

Detection parameters are set at runtime through the **修改配置** dialog:

| Parameter | Meaning |
|---|---|
| Model path | Path to a `.onnx` model file |
| Class names | One class per line (or comma-separated); order must match the model's output |
| Confidence threshold | Minimum score to keep a detection (0.01–0.99) |
| NMS threshold | IoU threshold for non-maximum suppression |
| Threads | Intra-op thread count (`0` = ORT default) |
| CUDA device id | GPU index used by the CUDA execution provider |
| Input width / height | Network input size; auto-corrected if it disagrees with the model |
| Enable CUDA | Try the CUDA EP; falls back to CPU automatically when unavailable |

**Sample model paths** are hard-coded in `src/main.cpp` (`AfricanWildlife_model_path`,
`People_model_path`). They point at the author's local training output and are only used by
the commented-out demo helpers — update or remove them for your own setup.

---

## Models

Any YOLO detection model exported to ONNX should work, provided that:

- the input is a single `float32` tensor of shape `[1, 3, H, W]` (dynamic `H`/`W` supported);
- the output is `[1, 4 + num_classes, num_anchors]` with `(cx, cy, w, h)` in letterbox space.

The class names you enter must be ordered exactly as in the model's training data.

Two sample assets are bundled through `resources/images.qrc`:

- `:/image1` — a sample image,
- `:/models/african-wildlifeModel` — a small African-wildlife detection model.

These are convenience defaults for trying the app out; the model you actually run comes from
the configuration dialog.

---

## Output

When **保存推理结果** is enabled, results are written to:

```
<chosen folder>/
└── detection_results/
    ├── <original name>.jpg     # image tasks
    └── <original name>.mp4     # video / camera tasks
```

Only annotated frames are written (no label files) — the purpose is visual review, not
training or evaluation. If saving fails, the error is reported once and **detection
continues**.

---

## Troubleshooting

| Symptom | Likely cause / fix |
|---|---|
| `Could NOT find OpenCV` at configure time | `OpenCV_DIR` points at a build made with a different toolchain (e.g. MinGW). Point it at an MSVC build containing `x64/vc16/lib`. |
| App fails to start, missing `*.dll` | Runtime DLLs were not deployed. Ensure the post-build copy step ran, or add OpenCV/ONNX Runtime `bin` folders to `PATH`. |
| Detections look shifted or scaled | Input size does not match the model. Check the warning banner — the app auto-aligns and reports the change. |
| CUDA not used | CUDA/cuDNN DLLs not found. Set `CUDA_PATH`/`CUDNN_PATH` (or `PATH`) so `DEPLOY_CUDA_RUNTIME` can find them; otherwise the CPU EP is used. |
| First inference is slow | Expected once — the app warms up the session at start-up, but the very first frame after a model change still pays session creation cost. |
| Configure fails to find `MvCameraControl` | The MVS SDK was not found. Install the Hikvision MVS SDK (it sets `MVCAM_COMMON_RUNENV`), or edit `MVS_SDK_ROOT` at the top of `CMakeLists.txt`. `MVS_CAMERA_CONTROL_LIB` is a required `find_library`, so this is a hard configure error. |
| App does not start at all after adding Hik camera support | The MVS runtime is missing — the exe imports `MvCameraControl.dll`, so a missing runtime is a start-up failure, not just an unavailable feature. `DEPLOY_MVS_RUNTIME` should have copied it next to the exe; if auto-detection found nothing, pass `-DMVS_RUNTIME_DIR=<dir>` or add the correct-architecture MVS runtime folder to `PATH`. Beware that MVS also installs a **32-bit** runtime and puts it on `PATH` too: a wrong-architecture DLL found first fails the load instead of falling through to the right one. |
| **海康相机检测** lists no devices | Camera not powered, not on the same subnet (GigE) or not connected (USB3), MVS runtime missing, or the camera is already opened by another process — e.g. the MVS client — since only one process can grab at a time. Try **刷新** after fixing, then check the log for the SDK error code. |

---

## License

Released under the [MIT License](LICENSE).

Third-party components keep their own licenses:

- **Qt** — LGPLv3 / commercial (see your Qt installation),
- **OpenCV** — Apache-2.0,
- **ONNX Runtime** — MIT.
