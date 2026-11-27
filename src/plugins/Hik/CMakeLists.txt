# -----------------------------------------------------------------------------
# 海康相机帧源插件（MVS SDK）
#
# 本目录是海康帧源的构建边界：
#   1. 探测 MVS SDK，把结果写进全局 YOLO_HAVE_HIK，供根 CMakeLists 决定
#      是否部署 MVS 运行时。
#   2. SDK 可用时把帧源实现与插件类一起编成 Qt 插件 DLL；exe 不链接任何
#      海康目标，运行时由 PluginManager 用 QPluginLoader 加载。
#
# 变量传播：add_subdirectory 里的普通 set() 只影响本目录，必须写 CACHE
# 才能让根作用域（add_subdirectory(src) 之后）读到。
# -----------------------------------------------------------------------------

# -----------------------------------------------------------------------------
# 纯 QML 模块：Hik
#
# 只含界面，不依赖 MVS；直接以磁盘文件形式部署，不生成 qrc。
# 目录结构必须是 <importPath>/<URI>；main.cpp 把 <exe>/plugins/framesource
# 加入引擎导入路径，因此这里是 <exe>/plugins/framesource/Hik。
# 插件 DLL 是否构建由下面的 YOLO_ENABLE_HIK 决定。
# -----------------------------------------------------------------------------
set(HIK_QML_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/qml")
set(HIK_QML_OUTPUT_DIR "${CMAKE_BINARY_DIR}/plugins/framesource/Hik")

add_custom_target(deploy_hik_qml ALL
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${HIK_QML_OUTPUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${HIK_QML_OUTPUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${HIK_QML_SOURCE_DIR}/HikCameraDialog.qml"
            "${HIK_QML_OUTPUT_DIR}/HikCameraDialog.qml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${HIK_QML_SOURCE_DIR}/qmldir"
            "${HIK_QML_OUTPUT_DIR}/qmldir"
    COMMENT "Deploying Hik QML module to ${HIK_QML_OUTPUT_DIR}"
    VERBATIM
)

option(YOLO_ENABLE_HIK "编译海康相机帧源插件（需要本机 MVS SDK）" ON)
set(YOLO_HAVE_HIK OFF CACHE INTERNAL "海康相机帧源可用性（由 MVS SDK 探测结果推导）")

# 用户显式关闭：不探测、不构建
if(NOT YOLO_ENABLE_HIK)
    message(STATUS "YOLO_ENABLE_HIK=OFF：跳过海康相机帧源")
    # QML 模块仍会部署（上面的 deploy_hik_qml），但上一次 ON 构建留下的插件 DLL
    # 不会被 CMake 自动删除，运行时仍会被 PluginManager 扫描到。每次 OFF 构建
    # 先清掉它，保证 PluginExists 与构建开关一致。
    add_custom_target(remove_stale_hik_plugin ALL
        COMMAND ${CMAKE_COMMAND} -E remove -f
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/framesource/hik_framesource_plugin.dll"
        COMMENT "Removing stale Hik plugin DLL"
        VERBATIM
    )
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

qt_add_plugin(hik_framesource_plugin
    CLASS_NAME HikFrameSourcePlugin
    HikFrameSourcePlugin.h
    HikFrameSourcePlugin.cpp
    HikCameraFrameSource.h
    HikCameraFrameSource.cpp
)

target_link_libraries(hik_framesource_plugin
    PRIVATE yolo_core 
    Qt6::Core Qt6::Qml
    ${MVS_CAMERA_CONTROL_LIB}
)
target_include_directories(hik_framesource_plugin PRIVATE ${MVS_INCLUDE_DIRS})
target_link_directories(hik_framesource_plugin PRIVATE ${MVS_LIBRARY_DIRS})

set_target_properties(hik_framesource_plugin PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/framesource"
    LIBRARY_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/framesource"
)
