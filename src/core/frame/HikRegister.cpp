// 注册Hik到抽象工厂中
#include "HikCameraFrameSource.h"
#include "FrameHandle.h"
#include "FrameSourceFactory.h"

FrameSourcePtr createHikCameraFrameSource( QStringView serial, CameraTriggerMode mode, MemoryType memoryType ) {
	using HikSource = HikCameraFrameSource;
	HikSource::TriggerMode m = HikSource::TriggerMode::Off;
	switch (mode) {
	case CameraTriggerMode::Off:      m = HikSource::TriggerMode::Off;     break;
	case CameraTriggerMode::Software: m = HikSource::TriggerMode::Software; break;
	case CameraTriggerMode::Line0:    m = HikSource::TriggerMode::Line0;   break;
	case CameraTriggerMode::Line1:    m = HikSource::TriggerMode::Line1;   break;
	case CameraTriggerMode::Line2:    m = HikSource::TriggerMode::Line2;   break;
	}
	return HikSource::forDevices( serial, m, memoryType );
}

// 用于上层注册Hik到抽象工厂中
HikCreator registerHikCameraFrameSource() {
	return &createHikCameraFrameSource;
}