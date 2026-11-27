# 海康相机帧源插件（MVS SDK）
# 设置QML模块路径
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
# 设置插件开关
option(YOLO_ENABLE_HIK "编译海康相机帧源插件（需要本机 MVS SDK）" ON)
set(YOLO_HAVE_HIK OFF CACHE INTERNAL "海康相机帧源可用性（由 MVS SDK 探测结果推导）")


if(NOT YOLO_ENABLE_HIK)
    message(STATUS "YOLO_ENABLE_HIK=OFF：跳过海康相机帧源")
    # 禁用插件后将原本的旧的DLL删除
    add_custom_target(remove_stale_hik_plugin ALL
        COMMAND ${CMAKE_COMMAND} -E remove -f
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/framesource/hik_framesource_plugin.dll"
        COMMENT "Removing stale Hik plugin DLL"
        VERBATIM
    )
    return()
endif()

# 检查是否存在MVS SDK
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
# 添加插件
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
