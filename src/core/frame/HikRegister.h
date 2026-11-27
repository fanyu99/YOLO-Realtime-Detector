#pragma once
#include "FrameSourceFactory.h"
#include <QStringView>

// 创建海康帧源：插件与上层工厂共用同一实现
FrameSourcePtr createHikCameraFrameSource ( QStringView serial, CameraTriggerMode mode, MemoryType memoryType );
// 取注册器回调，供 FrameSourceFactory::setHikCreator 注入
HikCreator registerHikCameraFrameSource ();
