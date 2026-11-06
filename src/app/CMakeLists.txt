# -----------------------------------------------------------------------------
# app 层：程序运行流程 + 程序运行组件
#
# 分组目录只是文件组织，不产生 target：
#   runtime/    DetectionApp(QML 门面)、DetectionManager(调度)、DetectionWorker
#   components/ Logger、DetectionRenderer、DetectionResultSaver、
#               DetectionImageProvider
#
# DetectionRuntime 只收 Worker/Renderer/Saver：三者构成执行单元，
# Worker 直接依赖 Renderer/Saver，拆开只会互相链接。
#
# DetectionApp / DetectionManager / Logger / DetectionImageProvider 是
# QML 模块的 SOURCES（Logger 带 QML_SINGLETON，注册必须发生在 exe 的
# qt_add_qml_module 中），由根 CMakeLists 直接登记进 QML 模块，不在本库；
# Logger.cpp 的符号由 exe 提供，本层与 core 仅通过 include 路径使用其头文件。
# -----------------------------------------------------------------------------
set(YOLO_APP_RUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/runtime")
set(YOLO_APP_COMPONENTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/components")

qt_add_library(DetectionRuntime STATIC
    # runtime/
    runtime/DetectionWorker.h
    runtime/DetectionWorker.cpp

    # components/
    components/DetectionRenderer.h
    components/DetectionRenderer.cpp
    components/DetectionResultSaver.h
    components/DetectionResultSaver.cpp
)

target_include_directories(DetectionRuntime PUBLIC
    "${YOLO_APP_RUNTIME_DIR}"
    "${YOLO_APP_COMPONENTS_DIR}"
)

target_link_libraries(DetectionRuntime PUBLIC
    yolo_core
    Qt::Core
    Qt::Gui
)
