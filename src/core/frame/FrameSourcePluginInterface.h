#pragma once
#include "FrameSource.h"
#include <QObject>
#include <QStringView>
#include <QVector>

// 帧源插件接口
class FrameSourcePluginInterface {
public:
	virtual ~FrameSourcePluginInterface () = default;
	// 插件标识
	virtual QString pluginId () const = 0;
	// 枚举当前在线设备
	virtual QVector<FrameSourceDeviceInfo> enumerateDevices () const = 0;
	// 创建帧源
	virtual FrameSourcePtr createFrameSource ( QStringView id, CameraTriggerMode mode, MemoryType memoryType ) const = 0;
};

#define FrameSourcePluginInterface_IID "com.yoloqt.FrameSourcePluginInterface"
Q_DECLARE_INTERFACE(FrameSourcePluginInterface, FrameSourcePluginInterface_IID)
