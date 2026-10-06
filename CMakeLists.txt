cmake_minimum_required(VERSION 3.16)
project(YOLO LANGUAGES CXX)

# Qt 版本兼容垫片（qt_standard_project_setup / qt_add_executable 等）。
# 注意用完整路径：include() 的参数若带 .cmake 扩展名会被当成「文件路径」，
# 不会去 CMAKE_MODULE_PATH 里搜索。
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/qt.cmake")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED)
find_package(onnxruntime CONFIG REQUIRED)
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR}
    COMPONENTS
        Quick
        Multimedia
)
qt_standard_project_setup()

# -----------------------------------------------------------------------------
# 源码搜索路径
#
# 各模块之间仍以「裸文件名」互相 include（例如 #include "Detector.h"），
# 因此这里把 src 及其子目录统一加入搜索路径。好处是源文件在 src/ 内挪动
# 目录时，不必逐个改写 include 语句。
# -----------------------------------------------------------------------------
set(YOLO_SOURCE_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/tools"
)

# -----------------------------------------------------------------------------
# 可执行程序入口 + 内嵌资源
# -----------------------------------------------------------------------------
set(PROJECT_SOURCES
    src/main.cpp
)
# 应用内嵌资源：默认示例图片与示例模型（rcc 按 .qrc 所在目录解析相对路径）
qt_add_resources(PROJECT_SOURCES resources/images.qrc)

# -----------------------------------------------------------------------------
# 静态库
# -----------------------------------------------------------------------------
# 推理内核：ONNX 会话管理、letterbox 预处理、YOLO 输出解析与 NMS
qt_add_library(Detector STATIC
    src/core/Detector.cpp
    src/core/Detector.h
)
# OpenCV 预览工具（独立于主界面的调试用 imshow 封装）
qt_add_library(ImageShower STATIC
    src/tools/ImageShower.cpp
    src/tools/ImageShower.h
)
# 执行单元：worker 线程循环 + 结果渲染 + 结果落盘
qt_add_library(DetectionWorker STATIC
    src/app/DetectionWorker.cpp       src/app/DetectionWorker.h
    src/app/DetectionRenderer.cpp     src/app/DetectionRenderer.h
    src/app/DetectionResultSaver.cpp  src/app/DetectionResultSaver.h
)

foreach(_lib Detector ImageShower DetectionWorker)
    target_include_directories(${_lib} PUBLIC ${YOLO_SOURCE_DIRS})
endforeach()

# -----------------------------------------------------------------------------
# 可执行程序
# -----------------------------------------------------------------------------
qt_add_executable(${PROJECT_NAME} ${PROJECT_SOURCES})

# QML 模块（URI = yolo）。
#
# QML 源文件位于 qml/ 子目录下。若不处理，资源路径会带上这层子目录
# （/qt/qml/yolo/qml/main.qml），Qt 还会因为「模块内多了一层目录」而提示
# QTP0004（要求每个额外目录都提供 qmldir）。
# 这里给每个 QML 文件显式指定资源别名 = 文件名，把资源路径压平回
# /qt/qml/yolo/<文件名>.qml —— 与文件物理位置解耦，engine.load() 的 URL 保持不变。
set(YOLO_QML_FILES
    qml/main.qml
    qml/Theme.qml
    qml/LogView.qml
    qml/ConfigDialog.qml
    qml/CameraDialog.qml
)
foreach(_qml IN LISTS YOLO_QML_FILES)
    get_filename_component(_qml_name "${_qml}" NAME)
    set_source_files_properties("${_qml}" PROPERTIES QT_RESOURCE_ALIAS "${_qml_name}")
endforeach()

if(QT_VERSION VERSION_GREATER_EQUAL 6.2)
    qt_add_qml_module(${PROJECT_NAME}
        URI yolo
        VERSION 1.0
        RESOURCE_PREFIX "/qt/qml/"
        QML_FILES
            ${YOLO_QML_FILES}
        SOURCES
            src/app/DetectionApp.cpp            src/app/DetectionApp.h
            src/app/DetectionManager.cpp        src/app/DetectionManager.h
            src/app/DetectionImageProvider.cpp  src/app/DetectionImageProvider.h
            src/app/Logger.cpp                  src/app/Logger.h
            src/core/DetectionTypes.h
            src/core/Box.h
            src/core/AppError.h
    )
endif()

set_target_properties(${PROJECT_NAME}
    PROPERTIES
        WIN32_EXECUTABLE TRUE
)

target_include_directories(${PROJECT_NAME} PRIVATE ${YOLO_SOURCE_DIRS})

# -----------------------------------------------------------------------------
# 链接
# -----------------------------------------------------------------------------
target_link_libraries(Detector PUBLIC onnxruntime ${OpenCV_LIBS} Qt::Core)
target_link_libraries(DetectionWorker PRIVATE Detector Qt::Core Qt::Gui)
target_link_libraries(ImageShower PUBLIC ${OpenCV_LIBS} Qt::Core)
target_link_libraries(${PROJECT_NAME}
    PUBLIC
        Qt::Quick
        Qt::Multimedia
        ${OpenCV_LIBS}
        onnxruntime
        Detector
        DetectionWorker
        ImageShower
)

if(Qt6_FOUND)
    qt_import_qml_plugins(${PROJECT_NAME})
endif()

# -----------------------------------------------------------------------------
# 运行时 DLL 部署（Windows）
#
# MSVC 只在导入表里记录 DLL 文件名，而这些 DLL 所在的 bin 目录不在 PATH 上，
# 程序启动时会直接加载失败。这里把依赖目录下的 DLL 全部复制到 exe 旁边。
# 用 glob 而不是逐个列文件：以后新增依赖（ffmpeg、CUDA provider 等）无需改这里。
# -----------------------------------------------------------------------------
if(WIN32)
    set(RUNTIME_DLL_DIRS "")
    foreach(_dep_target opencv_world onnxruntime)
        if(TARGET ${_dep_target})
            foreach(_cfg "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
                get_target_property(_loc ${_dep_target} "IMPORTED_LOCATION${_cfg}")
                if(_loc AND EXISTS "${_loc}")
                    get_filename_component(_dir "${_loc}" DIRECTORY)
                    list(APPEND RUNTIME_DLL_DIRS "${_dir}")
                endif()
            endforeach()
        endif()
    endforeach()
    list(REMOVE_DUPLICATES RUNTIME_DLL_DIRS)

    set(RUNTIME_DLLS "")
    foreach(_dir IN LISTS RUNTIME_DLL_DIRS)
        file(GLOB _dlls "${_dir}/*.dll")
        list(APPEND RUNTIME_DLLS ${_dlls})
    endforeach()

    if(RUNTIME_DLLS)
        list(LENGTH RUNTIME_DLLS _dll_count)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${RUNTIME_DLLS}
                $<TARGET_FILE_DIR:${PROJECT_NAME}>
            COMMENT "Deploying ${_dll_count} runtime DLLs next to ${PROJECT_NAME}.exe"
        )
    endif()
endif()

# CUDA / cuDNN 运行时 DLL 部署。
# onnxruntime_providers_cuda.dll 静态依赖 cublasLt64_13.dll / cublas64_13.dll，
# 运行时还要 LoadLibrary cudart64_13.dll / cudnn64_9.dll / nvrtc64_13x_0.dll。
# 这些 DLL 只由 CUDA Toolkit 和 cuDNN 提供，显卡驱动不带。
# 这里把它们一起复制到 exe 目录，免去每台机器手工配 PATH。
# 若已通过 PATH 提供，可用 -DDEPLOY_CUDA_RUNTIME=OFF 关闭。
option(DEPLOY_CUDA_RUNTIME "Copy CUDA/cuDNN runtime DLLs next to the executable" ON)
if(WIN32 AND DEPLOY_CUDA_RUNTIME)
    # 注意目录布局：CUDA 12 及更早把运行时 DLL 放在 bin\，
    # CUDA 13 起改到 bin\x64\；cuDNN 9.x 的压缩包是 bin\x64\。两个都搜，兼容新旧版本。
    set(_cuda_bin_dirs "")
    if(DEFINED ENV{CUDA_PATH})
        list(APPEND _cuda_bin_dirs "$ENV{CUDA_PATH}/bin" "$ENV{CUDA_PATH}/bin/x64")
    endif()
    if(CUDNN_PATH)
        list(APPEND _cuda_bin_dirs "${CUDNN_PATH}/bin" "${CUDNN_PATH}/bin/x64")
    elseif(DEFINED ENV{CUDNN_PATH})
        list(APPEND _cuda_bin_dirs "$ENV{CUDNN_PATH}/bin" "$ENV{CUDNN_PATH}/bin/x64")
    endif()
    file(GLOB _cuda_toolkit_bins
        "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v*/bin"
        "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v*/bin/x64")
    list(APPEND _cuda_bin_dirs ${_cuda_toolkit_bins})
    list(REMOVE_DUPLICATES _cuda_bin_dirs)

    set(_cuda_dlls "")
    foreach(_dir IN LISTS _cuda_bin_dirs)
        foreach(_pat
            "cublas64_13.dll"
            "cublasLt64_13.dll"
            "cudart64_13.dll"
            "nvrtc64_13*_0.dll"
            "nvrtc-builtins64_13*.dll"
            "nvJitLink*.dll"
            "cudnn*.dll")
            file(GLOB _found "${_dir}/${_pat}")
            list(APPEND _cuda_dlls ${_found})
        endforeach()
    endforeach()
    if(_cuda_dlls)
        list(REMOVE_DUPLICATES _cuda_dlls)
    endif()

    if(_cuda_dlls)
        list(LENGTH _cuda_dlls _cuda_dll_count)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_cuda_dlls}
                $<TARGET_FILE_DIR:${PROJECT_NAME}>
            COMMENT "Deploying ${_cuda_dll_count} CUDA/cuDNN runtime DLLs next to ${PROJECT_NAME}.exe"
        )
    else()
        message(STATUS
            "DEPLOY_CUDA_RUNTIME=ON, but no CUDA/cuDNN bin directory was found. "
            "Set CUDA_PATH (and CUDNN_PATH) or add them to PATH, otherwise the CUDA EP will fall back to CPU.")
    endif()
endif()
