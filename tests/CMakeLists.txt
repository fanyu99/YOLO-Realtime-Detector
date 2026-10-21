# -----------------------------------------------------------------------------
#  测试程序
# -----------------------------------------------------------------------------
set(CMAKE_AUTOMOC ON)

# 添加性能测试
# 1. 添加 NMS 性能测试
qt_add_executable(test_nms
    test_duration_nms.cpp
    ../src/app/Logger.cpp
    ../src/app/Logger.h
)
target_link_libraries(test_nms PRIVATE 
Detector Qt6::Core 
Qt6::Test Qt6::Qml ${OpenCV_LIBS})

add_test(NAME test_nms COMMAND $<TARGET_FILE:test_nms>)
