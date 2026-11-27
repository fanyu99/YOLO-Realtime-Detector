cmake_minimum_required(VERSION 3.16)
project(YOLO LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Qt 版本兼容垫片（qt_standard_project_setup / qt_add_executable 等）。
# 注意用完整路径：include() 的参数若带 .cmake 扩展名会被当成「文件路径」，
# 不会去 CMAKE_MODULE_PATH 里搜索。
# --------------------------------------------------------------------------
# 海康 MVS SDK
#
# 根目录取自环境变量 MVCAM_COMMON_RUNENV（MVS 安装器会设置它），
# 也可以在配置时用缓存变量覆盖：-DMVS_SDK_ROOT=<SDK 根目录>。
# 这里不再回退到某个本机路径：猜错目录只会让后续的链接错误更难定位。
# --------------------------------------------------------------------------
set(MVS_SDK_ROOT "$ENV{MVCAM_COMMON_RUNENV}" CACHE PATH
    "海康 MVS SDK 根目录（应包含 Includes/、Libraries/）")

# 根据目标架构选择 win32 / win64 库目录
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(MVS_ARCH "win64")
else()
    set(MVS_ARCH "win32")
endif()
set(MVS_INCLUDE_DIRS "${MVS_SDK_ROOT}/Includes")
set(MVS_LIBRARY_DIRS "${MVS_SDK_ROOT}/Libraries/${MVS_ARCH}")
set(CMAKE_AUTOMOC ON)

# -----------------------------------------------------------------------------
# 全局构建模式开关：一键在 Release / Debug 之间切换
#
#   -DYOLO_BUILD_DEBUG=OFF（默认）→ Release 编译 + OpenCV Release 库
#   -DYOLO_BUILD_DEBUG=ON          → Debug 编译   + OpenCV Debug 库（opencv_world500d 等）
#
# 一个开关同时决定「编译配置」与「OpenCV 包目录」，避免出现 Debug 编译却链接
# Release OpenCV 的组合：Debug 下头文件会把 cv:: 放进 cv::debug_build_guard 命名
# 空间，与 release 库的符号名不一致，会直接链接失败。
# -----------------------------------------------------------------------------
option(YOLO_BUILD_DEBUG
"ON = Debug 构建并使用 OpenCV debug 库；OFF = Release（默认）"
OFF
)
if(YOLO_BUILD_DEBUG)
    message(STATUS "YOLO_BUILD_DEBUG= ON : Debug 构建 + OpenCV Debug 库")
else()
    message(STATUS "YOLO_BUILD_DEBUG= OFF : Release 构建 + OpenCV Release 库")
endif()

# 包目录默认取同名环境变量，也可以在配置时用 -D 覆盖。
# 目标机把 OpenCV 装在标准位置（或已加入 CMAKE_PREFIX_PATH）时两者留空即可。
set(YOLO_OPENCV_RELEASE_DIR "$ENV{YOLO_OPENCV_RELEASE_DIR}" CACHE PATH "OpenCV Release 包根目录（含 OpenCVConfig.cmake）")
set(YOLO_OPENCV_DEBUG_DIR "$ENV{YOLO_OPENCV_DEBUG_DIR}" CACHE PATH "OpenCV Debug 包根目录（含 OpenCVConfig.cmake）")


if(YOLO_BUILD_DEBUG)
    set(_yolo_build_type "Debug")
    set(_yolo_opencv_dir "${YOLO_OPENCV_DEBUG_DIR}")
    set(_yolo_opencv_label "Debug")
    set(_yolo_opencv_var "YOLO_OPENCV_DEBUG_DIR")
else()
    set(_yolo_build_type "Release")
    set(_yolo_opencv_dir "${YOLO_OPENCV_RELEASE_DIR}")
    set(_yolo_opencv_label "Release")
    set(_yolo_opencv_var "YOLO_OPENCV_RELEASE_DIR")
endif()

# 单配置生成器（Ninja / Makefiles）靠 CMAKE_BUILD_TYPE 决定优化级别与 CRT；
# 多配置生成器（Visual Studio / Xcode）由 --config 或 IDE 选择，这里只决定 OpenCV 包。
get_property(_yolo_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(_yolo_multi_config)
    message(STATUS "多配置生成器：YOLO_BUILD_DEBUG 只选择 OpenCV 包，"
        "构建配置请用 --config Debug|Release 指定")
else()
    set(CMAKE_BUILD_TYPE "${_yolo_build_type}" CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

if(_yolo_opencv_dir)
    if(NOT EXISTS "${_yolo_opencv_dir}/OpenCVConfig.cmake")
        message(FATAL_ERROR
            "YOLO_BUILD_DEBUG=${YOLO_BUILD_DEBUG} 需要 OpenCV ${_yolo_opencv_label} 包，"
            "但在 ${_yolo_opencv_dir} 下找不到 OpenCVConfig.cmake。\n"
            "请改正 ${_yolo_opencv_var}（环境变量，或配置时用 -D${_yolo_opencv_var}=<包根目录> 传入）。")
    endif()

    # 必须 FORCE 覆盖缓存里的 OpenCV_DIR：否则上一次 configure 留下的路径会被沿用，
    # 切换开关后依旧链到旧包（find_package 优先读 cache）。
    set(OpenCV_DIR "${_yolo_opencv_dir}" CACHE PATH "OpenCV 包目录（由 YOLO_BUILD_DEBUG 决定）" FORCE)
    message(STATUS "构建模式: ${_yolo_build_type}；OpenCV: ${_yolo_opencv_label} (${OpenCV_DIR})")
else()
    # 未指定包目录时不预设 OpenCV_DIR，交给 find_package(OpenCV) 走 OpenCV_DIR /
    # CMAKE_PREFIX_PATH 等常规途径 —— 把 OpenCV 装在标准位置的机器无需任何配置。
    message(STATUS "构建模式: ${_yolo_build_type}；未指定 OpenCV ${_yolo_opencv_label} 包目录，"
        "由 find_package(OpenCV) 自行查找（可设环境变量 ${_yolo_opencv_var} 指定）")
endif()

# --------------------------------------------------------------------------
# FFmpeg
#
# 安装目录取自环境变量 FFMPEG_DIR，也可以在配置时用缓存变量覆盖：
# -DFFMPEG_ROOT=<FFmpeg 安装目录>
# ---------------------------------------------------------------------------
set(FFMPEG_ROOT "$ENV{FFMPEG_DIR}" CACHE PATH
    "FFmpeg 安装目录（应包含 include/、lib/、bin/）")
file(TO_CMAKE_PATH "${FFMPEG_ROOT}" FFMPEG_ROOT)

if(NOT FFMPEG_ROOT)
    message(FATAL_ERROR
        "未指定 FFmpeg 安装目录：请设置环境变量 FFMPEG_DIR，"
        "或配置时传入 -DFFMPEG_ROOT=<FFmpeg 安装目录>。")
endif()

set(FFMPEG_INCLUDE_DIR "${FFMPEG_ROOT}/include")
set(FFMPEG_LIBRARY_DIR "${FFMPEG_ROOT}/lib")
set(FFMPEG_RUNTIME_DIR "${FFMPEG_ROOT}/bin")

if(NOT EXISTS "${FFMPEG_INCLUDE_DIR}/libavcodec/avcodec.h")
    message(FATAL_ERROR
        "在 ${FFMPEG_INCLUDE_DIR} 下找不到 libavcodec/avcodec.h。\n"
        "请检查 FFMPEG_ROOT（当前为 ${FFMPEG_ROOT}），"
        "或设置环境变量 FFMPEG_DIR 指向正确的安装目录。")
endif()

# 使用 DLL 的 import lib（avcodec.lib 等），不要用 lib*.lib 静态库。
# 不含 postproc：FFmpeg 8 已从上游删除 libpostproc，本项目也从未使用它。
set(FFMPEG_COMPONENTS
    avcodec
    avformat
    avutil
    swscale
    swresample
    avfilter
    avdevice
)

foreach(component IN LISTS FFMPEG_COMPONENTS)
    if(NOT EXISTS "${FFMPEG_LIBRARY_DIR}/${component}.lib")
        message(FATAL_ERROR
            "找不到 ${FFMPEG_LIBRARY_DIR}/${component}.lib，"
            "FFmpeg 安装目录可能不完整。")
    endif()

    add_library(FFmpeg::${component} SHARED IMPORTED)
    set_target_properties(FFmpeg::${component} PROPERTIES
        IMPORTED_IMPLIB "${FFMPEG_LIBRARY_DIR}/${component}.lib"
        IMPORTED_LOCATION "${FFMPEG_RUNTIME_DIR}/${component}.dll"
        INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIR}"
    )
endforeach()



# --------------------------------------------------------------------------
# CUDA Toolkit（条件编译）
#
# 只给 yolo_core 里直接写 CUDA 代码的模块用（CudaDevice、后续 GpuMat 链路）。
# onnxruntime 的 CUDA EP 只在运行时 LoadLibrary CUDA DLL，链接期不需要它。
#
# 校验逻辑：
#   YOLO_ENABLE_CUDA=OFF          -> 跳过探测，YOLO_HAVE_CUDA=OFF
#   YOLO_ENABLE_CUDA=ON 且找到    -> YOLO_HAVE_CUDA=ON，正常编译 CUDA 代码
#   YOLO_ENABLE_CUDA=ON 但没找到  -> WARNING 降级，YOLO_HAVE_CUDA=OFF，
#                                    CUDA 源文件不参与编译，工程仍可构建
# C++ 侧配合同名宏 YOLO_HAVE_CUDA 做条件包含（target_compile_definitions 注入）。
# --------------------------------------------------------------------------
option(YOLO_ENABLE_CUDA "启用 CUDA 相关代码的编译（需要本机 CUDA Toolkit）" ON)
set(YOLO_HAVE_CUDA OFF)
if(YOLO_ENABLE_CUDA)
    find_package(CUDAToolkit QUIET)
    if(CUDAToolkit_FOUND)
        set(YOLO_HAVE_CUDA ON)
        # 计算能力按目标显卡改：120 = Blackwell（RTX 50 系）。
        # 编错架构的程序在别的卡上会直接跑不起来，所以留成可覆盖的缓存变量。
        set(CMAKE_CUDA_ARCHITECTURES "120" CACHE STRING
            "目标 GPU 的计算能力（如 89 / 120；多个用分号分隔）")
        enable_language(CUDA)
        set(CMAKE_CUDA_STANDARD 17)
        message(STATUS "CUDA编译开启：CUDA Toolkit: ${CUDAToolkit_VERSION} (${CUDAToolkit_LIBRARY_DIR})")
    else()
        message(WARNING
            "YOLO_ENABLE_CUDA=ON，但找不到 CUDA Toolkit（依赖 CUDA_PATH 或 PATH）。\n"
            "将跳过 cuda/ 目录源文件的编译；如需彻底关闭此探测，"
            "请配置 -DYOLO_ENABLE_CUDA=OFF。")
    endif()
endif()

# --------------------------------------------------------------------------
# Qt
#
# 安装目录取自环境变量 QTDIR（Qt 官方安装器与 Qt Creator presets 的约定），
# 也可以走 CMAKE_PREFIX_PATH 或 -DQt6_DIR=<Qt 安装目录>/lib/cmake/Qt6。
# --------------------------------------------------------------------------
if(DEFINED ENV{QTDIR})
    list(APPEND CMAKE_PREFIX_PATH "$ENV{QTDIR}")
endif()

find_package(OpenCV REQUIRED)
# 代码只用到 4.x 起就有的 core/imgproc/imgcodecs/videoio/dnn API，最低支持 4.0。
# 不能写成 find_package(OpenCV 4.0)：OpenCV 自带的 version 文件只按同一 major
# 判断兼容，那样会把 5.x 的包判成不兼容而直接失败。
if(OpenCV_VERSION VERSION_LESS 4.0)
    message(FATAL_ERROR
        "OpenCV ${OpenCV_VERSION} 过旧：本项目最低需要 4.0（当前包目录 ${OpenCV_DIR}）。")
endif()
find_package(Qt6 REQUIRED COMPONENTS Core Test)
# 海康 MVS SDK 的探测与插件目标在 src/plugins/HikFrameSource/CMakeLists.txt

# --------------------------------------------------------------------------
# ONNX Runtime
#
# 解压包根目录取自环境变量 ONNXRUNTIME_ROOT，支持两种布局：
#   1. 官方 GitHub release 包：lib/cmake/onnxruntime/ + include/ + lib/
#   2. 官方 NuGet 包（Microsoft.ML.OnnxRuntime）整理出的 include/ + lib/，
#      它不带 CMake 包配置，这里手工建出 onnxruntime target。
# 也可以直接指定包目录：-Donnxruntime_DIR=<包根目录>/lib/cmake/onnxruntime。
# --------------------------------------------------------------------------
set(ONNXRUNTIME_ROOT "$ENV{ONNXRUNTIME_ROOT}" CACHE PATH
    "ONNX Runtime 解压包根目录（含 include/、lib/）")
if(ONNXRUNTIME_ROOT AND NOT onnxruntime_DIR)
    set(onnxruntime_DIR "${ONNXRUNTIME_ROOT}/lib/cmake/onnxruntime" CACHE PATH
        "onnxruntime 的 CMake 包目录（由 ONNXRUNTIME_ROOT 推导）")
endif()

find_package(onnxruntime CONFIG QUIET)
if(NOT onnxruntime_FOUND AND ONNXRUNTIME_ROOT
   AND EXISTS "${ONNXRUNTIME_ROOT}/include/onnxruntime_cxx_api.h"
   AND EXISTS "${ONNXRUNTIME_ROOT}/lib/onnxruntime.lib")
    # 必须是 SHARED：CMake 对 UNKNOWN 类型会用 IMPORTED_LOCATION（DLL）去链接，
    # 只有 SHARED 才会在 Windows 上用 IMPORTED_IMPLIB（.lib）。
    add_library(onnxruntime SHARED IMPORTED)
    set_target_properties(onnxruntime PROPERTIES
        IMPORTED_IMPLIB "${ONNXRUNTIME_ROOT}/lib/onnxruntime.lib"
        IMPORTED_LOCATION "${ONNXRUNTIME_ROOT}/lib/onnxruntime.dll"
        INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_ROOT}/include")
    set(onnxruntime_FOUND TRUE)
endif()
if(NOT onnxruntime_FOUND)
    message(FATAL_ERROR
        "找不到 onnxruntime 包。\n"
        "请设置环境变量 ONNXRUNTIME_ROOT=<包根目录>（含 include/ 与 lib/，"
        "或含 lib/cmake/onnxruntime/），"
        "或配置时传入 -Donnxruntime_DIR=<包根目录>/lib/cmake/onnxruntime。")
endif()
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR}
    COMPONENTS
        Quick
        Multimedia
)
# 设置AUTOMOC等
qt_standard_project_setup()

message("成功找到OpenCV库: ${OpenCV_LIBS}")
message("成功找到ONNXRUNTIME库: ${onnxruntime_LIBS}")

# 当前这套 OpenCV 包里是否有 debug 库（*d.lib）。有则 Debug 配置可以正常链接。
get_target_property(_yolo_cv_debug_implib opencv_world IMPORTED_IMPLIB_DEBUG)
if(NOT _yolo_cv_debug_implib)
    # 多配置生成器随时可能用 --config Debug 编译，必须无条件兜底；
    # 单配置生成器则只有本次就是 Debug 时才需要。
    if(_yolo_multi_config OR YOLO_BUILD_DEBUG)
        message(WARNING
            "${OpenCV_DIR} 不带 OpenCV debug 库（无 *d.lib）。Debug 配置下将启用 "
            "CV_IGNORE_DEBUG_BUILD_GUARD 以兼容链接；若想调试 OpenCV，请设置 "
            "YOLO_BUILD_DEBUG=ON 并让它指向带 debug 库的包。")
        add_compile_definitions($<$<CONFIG:Debug>:CV_IGNORE_DEBUG_BUILD_GUARD>)
    endif()
endif()
unset(_yolo_cv_debug_implib)
# -----------------------------------------------------------------------------
# 源码搜索路径
#
# 各模块之间仍以「裸文件名」互相 include（例如 #include "CpuDetector.h"），
# 因此这里把 src 及其分组子目录统一加入搜索路径。好处是源文件在 src/ 内
# 挪动目录时，不必逐个改写 include 语句。
#
# 分组目录清单与 src/core、src/app 的 CMakeLists 保持一致：
#   core/base  core/inference  core/frame
#   app/runtime  app/components
# -----------------------------------------------------------------------------
set(YOLO_SOURCE_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/base"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/inference"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/frame"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/runtime"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/components"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/configs"
)

# 运行时 DLL 部署函数：主程序与 tests/ 下的测试程序共用。
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/deploy_runtime.cmake)

# -----------------------------------------------------------------------------
# 可执行程序入口 
# -----------------------------------------------------------------------------
set(PROJECT_SOURCES
    src/main.cpp
)

# -----------------------------------------------------------------------------
# 模块库：定义在 src/core/CMakeLists.txt 与 src/app/CMakeLists.txt 中
#   yolo_core        核心层（base + inference + frame，不含海康）
#   hik_framesource_plugin  海康相机帧源插件（src/plugins/HikFrameSource，私有依赖 MVS SDK）
#   DetectionRuntime 应用层运行库（runtime + components）
# -----------------------------------------------------------------------------
add_subdirectory(src)

# -----------------------------------------------------------------------------
#  开启测试、测试程序
# -----------------------------------------------------------------------------
option(BUILD_TESTS "performance_tests" ON) # 设置测试开关
enable_testing()
if(BUILD_TESTS )
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        message(STATUS "=============开启测试=============")
        add_subdirectory(tests)
    else()
        message(STATUS "=============未找到测试目录，跳过测试=============")
     endif()
endif()

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
    qml/windows/main.qml
    qml/components/DisplaySidebar.qml
    qml/components/DisplayMain.qml
    qml/windows/ConfigDialog.qml
    qml/windows/FfmpegDeviceDialog.qml
    qml/components/PerformanceRect.qml
    qml/windows/PerformanceView.qml
    qml/components/AppTheme.qml
    qml/components/LogView.qml
    qml/components/Shortcuts.qml
    qml/components/StatusBanner.qml
    qml/components/MediaFileDialog.qml
    qml/components/ThemedProgressBar.qml
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
                src/app/components/DetectionOverlayItem.h
                src/app/components/DetectionOverlayItem.cpp
                src/app/components/OverlayTextureSource.h
                src/app/components/OverlayTextureSource.cpp
            src/app/runtime/AppHelper.h
            src/app/runtime/PerformanceApp.h   src/app/runtime/PerformanceApp.cpp
            src/app/runtime/DetectionApp.cpp           src/app/runtime/DetectionApp.h
            src/app/runtime/DetectionManager.cpp       src/app/runtime/DetectionManager.h
            src/app/components/DetectionImageProvider.cpp  src/app/components/DetectionImageProvider.h
            src/app/components/Logger.cpp              src/app/components/Logger.h
            src/core/base/DetectionTypes.h
            src/core/base/DecodeTypes.h
            src/core/base/Box.h
            src/core/base/AppError.h
            src/core/base/DisplayPolicy.h
            src/core/base/DisplaySaveStatus.h
            src/app/configs/AppConfig.h
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
# 各模块自身的依赖已在定义处（src/core、src/app 的 CMakeLists）声明，
# 这里只负责把 exe 与三个模块库接起来。
target_link_libraries(${PROJECT_NAME}
    PUBLIC
        Qt::Quick
        Qt::Multimedia
        ${OpenCV_LIBS}
        onnxruntime
        yolo_core
        DetectionRuntime
)

# GPU 叠加层的 CUDA-D3D11 互操作需要系统库（仅 YOLO_HAVE_CUDA 构建才编译该路径）。
# QRhi 是半公开头，走 Qt 官方的 GuiPrivate 导出包拿版本化 private include 目录
# （<rhi/qrhi.h> 及其内部的 rhi/qshader.h 都在其中，版本升级无需改配置）
if(YOLO_HAVE_CUDA)
    find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
    target_link_libraries(${PROJECT_NAME} PUBLIC d3d11 dxgi Qt6::GuiPrivate)
endif()

# 海康帧源由插件在运行时加载，exe 不链接插件目标；仅下发可用性常量（0/1）。
if(YOLO_HAVE_HIK)
    target_compile_definitions(${PROJECT_NAME} PRIVATE YOLO_HAVE_HIK=1)
else()
    target_compile_definitions(${PROJECT_NAME} PRIVATE YOLO_HAVE_HIK=0)
endif()

if(Qt6_FOUND)
    qt_import_qml_plugins(${PROJECT_NAME})
endif()

# -----------------------------------------------------------------------------
# 运行时 DLL 部署（Windows）
#
# 具体实现见 cmake/deploy_runtime.cmake。主程序需要 OpenCV / ONNX Runtime /
# CUDA / MVS 全部依赖；测试程序只部署它实际用到的部分（在 tests/ 中调用）。
# -----------------------------------------------------------------------------
yolo_deploy_runtime_dlls(${PROJECT_NAME})

# 示例资源部署：examples/（示例图片 + 示例模型）随构建同步到 exe 目录，
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/examples"
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/examples
        COMMENT "Deploying examples/ (sample images + models) next to ${PROJECT_NAME}.exe"
    )
    message(STATUS "成功找到示例资源目录： ${CMAKE_CURRENT_SOURCE_DIR}/examples")
endif()

# 应用配置部署：settings/（app.json）随构建同步到 exe 目录，
# main.cpp 优先从 exe 目录查找 settings/app.json，这样无论从 IDE 还是
# 命令行启动（工作目录各不相同）都能读到同一份配置。
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/settings")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/settings"
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/settings
        COMMENT "Deploying settings/ (app.json) next to ${PROJECT_NAME}.exe"
    )
    message(STATUS "成功找到配置目录： ${CMAKE_CURRENT_SOURCE_DIR}/settings")
endif()

# CUDA / cuDNN 运行时 DLL 部署，实现见 cmake/deploy_runtime.cmake。
yolo_deploy_cuda_runtime(${PROJECT_NAME})

# 海康 MVS 运行时 DLL 部署，实现见 cmake/deploy_runtime.cmake。
# 只有帧源插件真正编入时才需要，SDK 缺失/用户关闭时不部署。
if(YOLO_HAVE_HIK)
    yolo_deploy_mvs_runtime(${PROJECT_NAME})
endif()


