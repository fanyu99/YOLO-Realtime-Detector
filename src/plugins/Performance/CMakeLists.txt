# 1. 内存插件
set(MEMORY_QML_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Memory/qml")
set(MEMORY_QML_OUTPUT_DIR "${CMAKE_BINARY_DIR}/plugins/performance/PerformanceMemoryMonitor")
add_custom_target(deploy_memory_qml ALL
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${MEMORY_QML_OUTPUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${MEMORY_QML_OUTPUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MEMORY_QML_SOURCE_DIR}/MemoryPanel.qml"
            "${MEMORY_QML_OUTPUT_DIR}/MemoryPanel.qml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MEMORY_QML_SOURCE_DIR}/qmldir"
            "${MEMORY_QML_OUTPUT_DIR}/qmldir"
    COMMENT "Deploying PerformanceMemoryMonitor QML module to ${MEMORY_QML_OUTPUT_DIR}"
    VERBATIM
)

option(YOLO_ENABLE_PERFORMANCE_MEMORY "编译内存监控模块" ON)
if(NOT YOLO_ENABLE_PERFORMANCE_MEMORY)
	message(STATUS "YOLO_ENABLE_PERFORMANCE_MEMORY = OFF :内存监控插件跳过")
	# 禁用插件后删除旧的动态库，避免运行时仍被加载
	add_custom_target(remove_stale_performance_memorymonitor_plugin ALL
		COMMAND ${CMAKE_COMMAND} -E remove -f
				"$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/performance/performance_memorymonitor_plugin.dll"
				"$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/performance/libperformance_memorymonitor_plugin.so"
		COMMENT "Removing stale memory monitor plugin binary"
		VERBATIM
	)
	return()
endif()

# 内存监控源路径
set(MemoryMonitorSources
	Memory/PerformanceMemoryMonitorPlugin.h
	Memory/PerformanceMemoryMonitorPlugin.cpp
	Memory/MemoryMonitor.h
)
if(WIN32)
	list(APPEND MemoryMonitorSources
		Memory/WinMemoryMonitor.h
		Memory/WinMemoryMonitor.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	list(APPEND MemoryMonitorSources
		Memory/LinuxMemoryMonitor.h
		Memory/LinuxMemoryMonitor.cpp)
endif()
qt_add_plugin(performance_memorymonitor_plugin
	CLASS_NAME PerformanceMemoryMonitorPlugin
	${MemoryMonitorSources}
)
target_link_libraries(performance_memorymonitor_plugin
	PRIVATE
	Qt6::Core
	yolo_core
)
if(WIN32)
	target_link_libraries(performance_memorymonitor_plugin
		PRIVATE psapi dxgi
	)
endif()
# 输出到主程序的插件目录，main.cpp 启动时递归扫描该目录
set_target_properties(performance_memorymonitor_plugin PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/performance"
	LIBRARY_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/performance"
)
