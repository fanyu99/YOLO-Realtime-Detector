
# ---------------------------------------------------------------------------
# 目标
# ---------------------------------------------------------------------------
qt_add_library(RtspDecodeWorker STATIC
    RtspDecodeWorker.cpp
    RtspDecodeWorker.h
)

qt_add_library(RtspImageProvider STATIC
    RtspImageProvider.cpp
    RtspImageProvider.h
)

qt_add_library(RtspStreamController STATIC
    RtspStreamController.cpp
    RtspStreamController.h
    RtspTypes.h
)



qt_add_executable(RtspPullerApp
    app_main.cpp
)

# 这里只把 main.qml 编进应用自己的 QML 模块（URI 决定资源路径
# qrc:/qt/qml/RtspPuller/main.qml）。
qt_add_qml_module(RtspPullerApp
    URI RtspPuller
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml/"
    QML_FILES main.qml
)

target_link_libraries(RtspPullerApp PRIVATE
    RtspStreamController
    RtspImageProvider
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
)
set_target_properties(RtspPullerApp PROPERTIES WIN32_EXECUTABLE TRUE)

target_link_libraries(RtspDecodeWorker PUBLIC
    Qt6::Core
    Qt6::Gui
    FFmpeg::avcodec
    FFmpeg::avformat
    FFmpeg::avutil
    FFmpeg::swscale
    FFmpeg::swresample
    FFmpeg::avfilter
    FFmpeg::avdevice
    FFmpeg::postproc
)
target_link_libraries(RtspImageProvider PUBLIC
    Qt6::Core
    Qt6::Quick
    Qt6::Gui
)
target_link_libraries(RtspStreamController PUBLIC
    Qt6::Core
    Qt6::Quick
    Qt6::Qml
    RtspDecodeWorker

)

# 构建后把 FFmpeg DLL 复制到 exe 旁边，双击或在 IDE 里运行都不依赖 PATH。
foreach(_target IN ITEMS  RtspPullerApp)
    foreach(component IN LISTS FFMPEG_COMPONENTS)
        add_custom_command(TARGET ${_target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${FFMPEG_RUNTIME_DIR}/${component}.dll"
                "$<TARGET_FILE_DIR:${_target}>"
            COMMENT "复制 ${component}.dll -> ${_target}"
            VERBATIM
        )
    endforeach()
endforeach()
