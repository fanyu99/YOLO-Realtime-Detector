cmake_minimum_required(VERSION 3.16)
project(YOLO LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Qt 版本兼容垫片（qt_standard_project_setup / qt_add_executable 等）。
# 注意用完整路径：include() 的参数若带 .cmake 扩展名会被当成「文件路径」，
# 不会去 CMAKE_MODULE_PATH 里搜索。
set(MVS_SDK_ROOT "$ENV{MVCAM_COMMON_RUNENV}")
if(NOT MVS_SDK_ROOT)
    set(MVS_SDK_ROOT "D:/MVS/Development")
endif()

# 根据目标架构选择 win32 / win64 库目录
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(MVS_ARCH "win64")
else()
    set(MVS_ARCH "win32")
endif()

set(MVS_INCLUDE_DIRS "${MVS_SDK_ROOT}/Includes")
set(MVS_LIBRARY_DIRS "${MVS_SDK_ROOT}/Libraries/${MVS_ARCH}")
set(CMAKE_AUTOMOC ON)
find_package(OpenCV REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core)
# 查找 MvCameraControl.lib（海康相机控制核心库）
find_library(MVS_CAMERA_CONTROL_LIB
    NAMES MvCameraControl
    PATHS "${MVS_LIBRARY_DIRS}"
    NO_DEFAULT_PATH
    REQUIRED
)

find_package(OpenCV REQUIRED)
find_package(onnxruntime CONFIG REQUIRED)
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR}
    COMPONENTS
        Quick
        Multimedia
)

qt_standard_project_setup()

message("成功找到OpenCV库: ${OpenCV_LIBS}")
message("成功找到ONNXRUNTIME库: ${onnxruntime_LIBS}")
message("成功找到MVS库: ${MVS_LIBRARY_DIRS}")
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
# 帧来源单元: 1.摄像头/视频文件/图片文件 2.其他相机
# 策略模式：FrameSource 作为接口，
# FileFrameSource/HikCameraFrameSource 作为具体实现
qt_add_library(FrameSource INTERFACE
    src/core/FrameSource.h
)
# 1.摄像头/视频文件/图片文件帧来源
qt_add_library(FileFrameSource STATIC 
src/core/FileFrameSource.h  src/core/FileFrameSource.cpp)
# 2.其他相机帧来源
qt_add_library(HikCameraFrameSource STATIC 
src/core/HikCameraFrameSource.h src/core/HikCameraFrameSource.cpp)

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
    qml/HikCameraDialog.qml
    qml/AcceptShortcut.qml
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

target_include_directories(${PROJECT_NAME} 
PRIVATE ${YOLO_SOURCE_DIRS})

# -----------------------------------------------------------------------------
# 链接
# -----------------------------------------------------------------------------
target_link_libraries(Detector PUBLIC onnxruntime ${OpenCV_LIBS} Qt::Core)
target_link_libraries(ImageShower PUBLIC ${OpenCV_LIBS} Qt::Core)
target_include_directories(FrameSource INTERFACE
${YOLO_SOURCE_DIRS}
${OpenCV_INCLUDE_DIRS}
)
target_link_libraries(FileFrameSource PUBLIC FrameSource)
target_link_libraries(HikCameraFrameSource 
PUBLIC FrameSource 
PRIVATE ${MVS_CAMERA_CONTROL_LIB})
target_include_directories(HikCameraFrameSource 
PRIVATE ${MVS_INCLUDE_DIRS})
target_link_directories(HikCameraFrameSource
PRIVATE  ${MVS_LIBRARY_DIRS})
# Qt::Core 供 QString；OpenCV 供 VideoCapture/imread
target_link_libraries(FrameSource INTERFACE 
${OpenCV_LIBS} Qt::Core )
target_link_libraries(DetectionWorker PRIVATE Detector Qt::Core Qt::Gui FrameSource)
target_link_libraries(${PROJECT_NAME}
    PUBLIC
        Qt::Quick
        Qt::Multimedia 
        ${OpenCV_LIBS}
        onnxruntime
        Detector
        DetectionWorker
        FrameSource
        HikCameraFrameSource
        FileFrameSource
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

# -----------------------------------------------------------------------------
# 海康 MVS 运行时 DLL 部署
#
# exe 的导入表里记录了 MvCameraControl.dll，它自己又依赖 GenICam 栈
# (GCBase/GenApi/XmlParser/...)和传输层(MvGigEVisionSDK / MvUsb3vTL)——
# 少任何一个都是「双击没反应」级别的启动失败，而不是海康功能不可用。
#
# MVS 安装器会把这些放进 Common Files 并加进 PATH，看起来不需要部署，但：
#   1) 那台机器必须装过 MVS SDK —— 只装运行时或走绿色包就未必；
#   2) MVS 会同时把 Win32_i86 和 Win64_x64 都加进 PATH，而 32 位那个常排在
#      前面。按 PATH 顺序查找是先命中名字、再校验架构的，命中一份错架构的
#      就是加载失败，不会继续往后找。
# 所以这里把正确架构的运行时直接复制到 exe 旁边 —— exe 目录在搜索顺序里
# 优先级最高，从此与 PATH 无关。
#
# 整个目录一起复制(约 110MB，大头是 IPP/SuperRender 这些给 MVS 自带查看器
# 用的库)。不想背这个体积就 -DDEPLOY_MVS_RUNTIME=OFF，自行保证 PATH。
# -----------------------------------------------------------------------------
option(DEPLOY_MVS_RUNTIME "Copy Hikvision MVS runtime DLLs next to the executable" ON)
if(WIN32 AND DEPLOY_MVS_RUNTIME)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_mvs_runtime_arch "Win64_x64")
    else()
        set(_mvs_runtime_arch "Win32_i86")
    endif()

    # 安装位置随版本/安装选项变，按常见顺序找第一个含 MvCameraControl.dll 的目录。
    # 注意不能用 $ENV{CommonProgramFiles(x86)} —— CMake 的 $ENV{} 解析不了带括号的
    # 变量名，会直接报语法错误。MVS 是 32 位安装器，固定装到 Common Files (x86)，
    # 所以这里直接写死标准路径，另外留一个 cache 变量给非标准安装。
    set(MVS_RUNTIME_DIR "" CACHE PATH "海康 MVS 运行时目录(留空则自动探测)")
    if(NOT MVS_RUNTIME_DIR)
        set(_mvs_runtime_candidates
            "${MVS_SDK_ROOT}/Runtime/${_mvs_runtime_arch}"
            "C:/Program Files (x86)/Common Files/MVS/Runtime/${_mvs_runtime_arch}"
            "C:/Program Files/Common Files/MVS/Runtime/${_mvs_runtime_arch}"
        )
        foreach(_dir IN LISTS _mvs_runtime_candidates)
            if(NOT MVS_RUNTIME_DIR AND EXISTS "${_dir}/MvCameraControl.dll")
                set(MVS_RUNTIME_DIR "${_dir}")
            endif()
        endforeach()
    endif()

    if(MVS_RUNTIME_DIR)
        file(GLOB _mvs_files "${MVS_RUNTIME_DIR}/*.dll" "${MVS_RUNTIME_DIR}/*.ini")
        list(LENGTH _mvs_files _mvs_count)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_mvs_files}
                $<TARGET_FILE_DIR:${PROJECT_NAME}>
            COMMENT "Deploying ${_mvs_count} MVS runtime files next to ${PROJECT_NAME}.exe"
        )
        message(STATUS "MVS 运行时目录: ${MVS_RUNTIME_DIR}")
    else()
        message(STATUS
            "DEPLOY_MVS_RUNTIME=ON, but no MVS runtime directory was found. "
            "MvCameraControl.dll will have to come from PATH; unused if you don't use Hik cameras.")
    endif()
endif()
