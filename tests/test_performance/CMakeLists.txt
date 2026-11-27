# 性能测试：
# 1.测试预览模式矩阵各个阶段计时显示
#
# 测试直接驱动 DetectionWorker，不链接 DetectionApp/DetectionManager
# （它们属于 exe 的 QML 模块，测试进程里拿不到）。
# 额外源文件：
#   Logger.cpp              Worker / CUDA 路径的日志符号
#   OverlayTextureSource   真实的 CUDA-D3D11 互操作与 CPU 上传纹理路径
#   AppConfig.h            测试用 AppConfigLoader 读取 app.json
#   DetectionTypes.h/Box.h AppConfig 属性的 Q_GADGET moc
qt_add_executable(test_display_matrix
	test_display_matrix.cpp
	${CMAKE_SOURCE_DIR}/src/app/components/Logger.cpp
	${CMAKE_SOURCE_DIR}/src/app/components/Logger.h
	${CMAKE_SOURCE_DIR}/src/app/components/OverlayTextureSource.cpp
	${CMAKE_SOURCE_DIR}/src/app/components/OverlayTextureSource.h
	${CMAKE_SOURCE_DIR}/src/app/configs/AppConfig.h
	${CMAKE_SOURCE_DIR}/src/core/base/DetectionTypes.h
	${CMAKE_SOURCE_DIR}/src/core/base/Box.h
)

target_include_directories(test_display_matrix PRIVATE
	${CMAKE_SOURCE_DIR}/src/app/configs
)

# 链接库
target_link_libraries(test_display_matrix PUBLIC
	Qt6::Test
	Qt6::Core
	Qt6::Gui
	Qt6::Qml
	Qt6::Quick
	yolo_core
	DetectionRuntime
	${OpenCV_LIBS}
)

# OverlayTextureSource 的 CUDA-D3D11 路径使用 QRhi 半公开头，需要 GuiPrivate
if(YOLO_HAVE_CUDA)
	find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
	target_link_libraries(test_display_matrix PUBLIC Qt6::GuiPrivate d3d11 dxgi)
endif()

# 运行时依赖部署（实现见 cmake/deploy_runtime.cmake）。
# 测试与主程序输出到同一目录，单独构建测试时也必须把依赖 DLL 放过去，
# 否则启动即报「找不到 opencv_world500d.dll」。
# MVS 不部署：测试没有链接海康插件，不需要海康运行时。
yolo_deploy_runtime_dlls(test_display_matrix)
yolo_deploy_cuda_runtime(test_display_matrix)

# 添加测试
add_test(NAME test_display_matrix COMMAND test_display_matrix)
# 4K + CPU 流程较慢，给足超时时间
set_tests_properties(test_display_matrix PROPERTIES TIMEOUT 7200)
