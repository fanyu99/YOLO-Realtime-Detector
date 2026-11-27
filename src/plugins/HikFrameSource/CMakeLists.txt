# -----------------------------------------------------------------------------
# 海康相机帧源插件（MVS SDK）
#
# 本目录是海康帧源的构建边界，负责：
#   1. 探测 MVS SDK，把结果写进全局 YOLO_HAVE_HIK，供根 CMakeLists 决定
#      是否链接、是否部署 MVS 运行时、是否定义编译宏。
#   2. SDK 可用时构建 yolo_hik（过渡期仍由 exe 直接链接，B 阶段改由插件消费）。
#   3. B 阶段把 HikFrameSourcePlugin 类加入本目录后，在这里追加 qt_add_plugin，
#      产出的 DLL 由 FrameSourcePluginHost 运行时加载。
#
# 变量传播：add_subdirectory 里的普通 set() 只影响本目录，必须写 CACHE
# 才能让根作用域（add_subdirectory(src) 之后）读到。
# -----------------------------------------------------------------------------

option(YOLO_ENABLE_HIK "编译海康相机帧源插件（需要本机 MVS SDK）" ON)
set(YOLO_HAVE_HIK OFF CACHE INTERNAL "海康相机帧源可用性（由 MVS SDK 探测结果推导）")

# 用户显式关闭：不探测、不构建
if(NOT YOLO_ENABLE_HIK)
    message(STATUS "YOLO_ENABLE_HIK=OFF：跳过海康相机帧源")
    return()
endif()

# SDK 缺失不中断整体构建，只降级跳过本插件（与 CUDA 探测同一策略）。
# 这里不改写 YOLO_ENABLE_HIK：用户意图与实际可用性分开，装好 SDK 后
# 重新 configure 即可恢复，不需要再手动打开开关。
if(NOT EXISTS "${MVS_LIBRARY_DIRS}/MvCameraControl.lib")
    message(WARNING
        "YOLO_ENABLE_HIK=ON，但 ${MVS_LIBRARY_DIRS} 下没有 MvCameraControl.lib。\n"
        "将跳过海康相机帧源。请设置环境变量 MVCAM_COMMON_RUNENV，"
        "或用 -DMVS_SDK_ROOT=<SDK 根目录> 指定；装好 SDK 后重新 configure 即可。")
    return()
endif()

set(YOLO_HAVE_HIK ON CACHE INTERNAL "海康相机帧源可用性（由 MVS SDK 探测结果推导）")
message(STATUS "海康 MVS SDK: ${MVS_LIBRARY_DIRS}/MvCameraControl.lib")

find_library(MVS_CAMERA_CONTROL_LIB
    NAMES MvCameraControl
    PATHS "${MVS_LIBRARY_DIRS}"
    NO_DEFAULT_PATH
    REQUIRED
)

# 过渡期：帧源实现仍在 core/frame，用静态库收口；B 阶段迁移到本目录时
# 只需改这里的源文件路径，exe 与插件的依赖关系不变。
qt_add_library(yolo_hik STATIC
    ../../core/frame/HikCameraFrameSource.h
    ../../core/frame/HikCameraFrameSource.cpp
)

target_include_directories(yolo_hik PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../.."
    "${CMAKE_CURRENT_SOURCE_DIR}/../../core/frame"
)

target_link_libraries(yolo_hik
    PUBLIC yolo_core
    PRIVATE ${MVS_CAMERA_CONTROL_LIB}
)
target_include_directories(yolo_hik PRIVATE ${MVS_INCLUDE_DIRS})
target_link_directories(yolo_hik PRIVATE ${MVS_LIBRARY_DIRS})

# ---- Qt 插件目标 ------------------------------------------------------------
# QPluginLoader 由宿主按 IID 加载，exe 不链接本目标。
# 输出到 <exe>/plugins/framesource，与宿主扫描路径保持一致。
qt_add_plugin(hik_framesource_plugin
    CLASS_NAME HikFrameSourcePlugin
    HikFrameSourcePlugin.h
    HikFrameSourcePlugin.cpp
)
target_link_libraries(hik_framesource_plugin
    PRIVATE yolo_hik Qt6::Core
)
set_target_properties(hik_framesource_plugin PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/framesource"
    LIBRARY_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/framesource"
)
