#include "HikFrameSourcePlugin.h"
#include "HikCameraFrameSource.h"
#include "HikRegister.h"

QString HikFrameSourcePlugin::pluginId () const {
	return QStringLiteral ( "hik" );
}

QVector<FrameSourceDeviceInfo> HikFrameSourcePlugin::enumerateDevices () const {
	return HikCameraFrameSource::enumerateDevices ();
}

FrameSourcePtr HikFrameSourcePlugin::createFrameSource ( QStringView id, CameraTriggerMode mode, MemoryType memoryType ) const {
	return createHikCameraFrameSource ( id, mode, memoryType );
}
