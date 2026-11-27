#pragma once
#include <QObject>
#include "FrameSourcePluginInterface.h"

// 海康帧源插件
class HikFrameSourcePlugin final : public QObject, public FrameSourcePluginInterface {
	Q_OBJECT
	Q_PLUGIN_METADATA ( IID "com.yoloqt.FrameSourcePluginInterface" )
	Q_INTERFACES ( FrameSourcePluginInterface )
public:
	QString pluginId () const override;
	QVector<FrameSourceDeviceInfo> enumerateDevices () const override;
	FrameSourcePtr createFrameSource ( QStringView id, CameraTriggerMode mode, MemoryType memoryType ) const override;
};
