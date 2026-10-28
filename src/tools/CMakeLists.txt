
# ---------------------------------------------------------------------------
# 目标
# ---------------------------------------------------------------------------
qt_add_library(RtspDecodeWorker STATIC 
    RtspDecodeWorker.cpp
    RtspDecodeWorker.h
    RtspTypes.h
)
qt_add_library(RtspImageProvider STATIC 
    RtspImageProvider.cpp
    RtspImageProvider.h
)

qt_add_library(RtspStreamController STATIC 
    RtspStreamController.cpp
    RtspStreamController.h
)



qt_add_executable(ffmpeg_learning
    main.cpp
)

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
)
target_link_libraries(RtspStreamController PUBLIC
    Qt6::Core
    Qt6::Quick
    Qt6::Qml
    RtspDecodeWorker

)

target_link_libraries(ffmpeg_learning PRIVATE
    RtspStreamController
    Qt6::Core
)

# 构建后把 FFmpeg DLL 复制到 exe 旁边，双击或在 IDE 里运行都不依赖 PATH。
foreach(component IN LISTS FFMPEG_COMPONENTS)
    add_custom_command(TARGET ffmpeg_learning POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${FFMPEG_RUNTIME_DIR}/${component}.dll"
            "$<TARGET_FILE_DIR:ffmpeg_learning>"
        COMMENT "复制 ${component}.dll"
        VERBATIM
    )
endforeach()
