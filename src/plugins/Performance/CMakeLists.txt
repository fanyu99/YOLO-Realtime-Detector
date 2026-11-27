# 1. 内存插件
# 内存监控源路径
set(MemoryMonitorSources 
	Memory/PerformanceMemoryMonitorPlugin.h
	Memory/PerformanceMemoryMonitorPlugin.cpp
	Memory/MemoryMonitor.h
)
qt_add_plugin(performance_memorymonitor_plugin
	CLASS_NAME PerformanceMemoryMonitorPlugin
	${MemoryMonitorSources}
)
option(YOLO_ENABLE_PERFORMANCE_MEMORY "编译内存监控模块" ON )
if(NOT YOLO_ENABLE_PERFORMANCE_MEMORY)
	message(STATUS "YOLO_ENABLE_PERFORMANCE_MEMORY = OFF :内存监控插件跳过")
	 add_custom_target(remove_stale_performance_memorymonitor_plugin ALL
        COMMAND ${CMAKE_COMMAND} -E remove -f
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/framesource/performance_memorymonitor_plugin.dll"
        COMMENT "删除旧动态库"
        VERBATIM
    )
	return()
endif()
if(WIN32)
	list(APPEND MemoryMonitorSources 
        Memory/WinMemoryMonitor.h
        Memory/WinMemoryMonitor.cpp)
    target_link_libraries(performance_memorymonitor_plugin 
	PRIVATE psapi dxgi 
	)
endif()
target_link_libraries(performance_memorymonitor_plugin
	PRIVATE
	Qt6::Core
	yolo_core
)

