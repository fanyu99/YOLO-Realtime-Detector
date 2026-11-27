#include "HikFrameSourcePlugin.h"
#include "HikCameraFrameSource.h"

namespace {
	// 将 CameraTriggerMode 转为 HikCameraFrameSource::TriggerMode
	HikCameraFrameSource::TriggerMode toHikTriggerMode ( CameraTriggerMode mode ) {
		using HikTrigger = HikCameraFrameSource::TriggerMode;
		switch (mode) {
		case CameraTriggerMode::Off:      return HikTrigger::Off;
		case CameraTriggerMode::Software: return HikTrigger::Software;
		case CameraTriggerMode::Line0:    return HikTrigger::Line0;
		case CameraTriggerMode::Line1:    return HikTrigger::Line1;
		case CameraTriggerMode::Line2:    return HikTrigger::Line2;
		}
		return HikTrigger::Off;
	}
}

QString HikFrameSourcePlugin::pluginId () const {
	return QStringLiteral ( "hik" );
}

QVector<FrameSourceDeviceInfo> HikFrameSourcePlugin::enumerateDevices () const {
	return HikCameraFrameSource::enumerateDevices ();
}

FrameSourcePtr HikFrameSourcePlugin::createFrameSource ( QStringView id, CameraTriggerMode mode, MemoryType memoryType ) const {
	if (id.isEmpty ())
		return nullptr;
	return HikCameraFrameSource::forDevices ( id, toHikTriggerMode ( mode ), memoryType );
}

FrameSourcePluginCallback HikFrameSourcePlugin::getFrameSourceCallback () const {
	return [ ] ( const FrameSourceDeviceInfo& info ) -> FrameSourcePtr {
		if (info.serial.isEmpty ())
			return nullptr;
		return HikCameraFrameSource::forDevices (
			info.serial, toHikTriggerMode ( info.triggerMode ), MemoryType::CPU );
	};
}
