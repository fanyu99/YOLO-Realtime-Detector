# -----------------------------------------------------------------------------
# core 层：基础类型 + 推理流程 + 帧源
#
# 分组目录只是文件组织，不产生 target：
#   base/      AppError、Box、DecodeTypes、DetectionTypes 等纯数据类型
#   inference/ IDetectionFlow、CpuDetectionFlow、CpuDetector、
#              CpuPreprocessor、CommonPostProcessor
#   frame/     FrameSource 及各帧源实现、FfmpegDecoder、FrameHandle
#
# 模块间 include 一律裸文件名（#include "CpuDetector.h"），因此这里把
# src 及所有分组目录加入 PUBLIC 搜索路径——源文件挪目录无需改 include。
#
# 海康相机源文件单独拆成 yolo_hik：它私有依赖 MVS SDK，与 OpenCV/ONNX
# 的依赖互不污染；未装 SDK 的构建可整体排除（YOLO_ENABLE_HIK）。
# -----------------------------------------------------------------------------
set(YOLO_CORE_BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/base")
set(YOLO_CORE_INFERENCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/inference")
set(YOLO_CORE_FRAME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/frame")

option(YOLO_ENABLE_HIK "编译海康相机帧源（需要本机 MVS SDK）" ON)

# ---- yolo_core：除海康外的全部核心代码 --------------------------------------
qt_add_library(yolo_core STATIC
    # base/
    base/AppError.h
    base/Box.h
    base/DecodeTypes.h
    base/DetectionTypes.h

    # inference/
    inference/IDetectionFlow.h
    inference/CpuDetectionFlow.h
    inference/CpuDetectionFlow.cpp
    inference/CpuDetector.h
    inference/CpuDetector.cpp
    inference/CpuPreprocessor.h
    inference/CpuPreprocessor.cpp
    inference/CommonPostProcessor.h
    inference/CommonPostProcessor.cpp

    # frame/
    frame/FrameHandle.h
    frame/CpuFrameHandle.h
    frame/CpuFrameHandle.cpp
    frame/FrameSource.h
    frame/FileFrameSource.h
    frame/FileFrameSource.cpp
    frame/FfmpegDecoder.h
    frame/FfmpegDecoder.cpp
    frame/FfmpegFrameSource.h
    frame/FfmpegFrameSource.cpp
)

target_include_directories(yolo_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${YOLO_CORE_BASE_DIR}"
    "${YOLO_CORE_INFERENCE_DIR}"
    "${YOLO_CORE_FRAME_DIR}"
    # core 的源文件（FileFrameSource/CpuDetectionFlow 等）通过裸文件名
    # include "Logger.h"（仅头文件，符号由 exe 的 QML 模块提供）
    "${CMAKE_CURRENT_SOURCE_DIR}/../app/components"
)

target_link_libraries(yolo_core PUBLIC
    Qt::Core
    Qt::Gui
    ${OpenCV_LIBS}
    onnxruntime
    FFmpeg::avcodec
    FFmpeg::avformat
    FFmpeg::avutil
    FFmpeg::swscale
    FFmpeg::swresample
    FFmpeg::avfilter
    FFmpeg::avdevice
    FFmpeg::postproc
)

# ---- yolo_hik：海康相机帧源，私有依赖 MVS SDK -------------------------------
if(YOLO_ENABLE_HIK)
    find_library(MVS_CAMERA_CONTROL_LIB
        NAMES MvCameraControl
        PATHS "${MVS_LIBRARY_DIRS}"
        NO_DEFAULT_PATH
        REQUIRED
    )

    qt_add_library(yolo_hik STATIC
        frame/HikCameraFrameSource.h
        frame/HikCameraFrameSource.cpp
    )

    target_include_directories(yolo_hik PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/.."
        "${YOLO_CORE_FRAME_DIR}"
    )

    target_link_libraries(yolo_hik
        PUBLIC yolo_core
        PRIVATE ${MVS_CAMERA_CONTROL_LIB}
    )
    target_include_directories(yolo_hik PRIVATE ${MVS_INCLUDE_DIRS})
    target_link_directories(yolo_hik PRIVATE ${MVS_LIBRARY_DIRS})
endif()
