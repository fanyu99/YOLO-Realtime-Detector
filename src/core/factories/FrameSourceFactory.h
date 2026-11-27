#pragma once
#include <QString>
#include <functional>
#include  "DetectionTypes.h"
#include "FrameHandle.h"
#include "FrameSource.h"
#include "FrameSourcePluginInterface.h"


// 抽象帧源工厂类
class FrameSourceFactory {
public:
	virtual ~FrameSourceFactory() = default;
	// 1.创建图片帧源（cv::imread 解码， CPU 帧句柄）
	virtual FrameSourcePtr createImageFrameSource( QStringView filePath ) = 0;
	// 创建视频帧源（ffmpeg，默认CPU）
	virtual FrameSourcePtr createVideoFrameSource( QStringView filePath, MemoryType memoryType = MemoryType::CPU ) = 0;
	// 创建摄像头帧源(ffmpeg)
	virtual FrameSourcePtr createCameraFrameSource(const QString& nameOrId, const QString& formatName = QString(), MemoryType memoryType = MemoryType::CPU) = 0;

	// 帧源插件
	// 添加帧源插件
	virtual void setPluginFrameSourcePlugin ( const QStringList& pluginIds, FrameSourcePluginCallback callback )
	{
		for (const auto& id : pluginIds)
		{
			if (!m_pluginCallbacks.contains ( id ))
			{
				m_pluginCallbacks.insert ( id, callback );
			}
		}
	}
	virtual void appendFrameSourcePlugin ( const QString& pluginId, FrameSourcePluginCallback callback )
	{
		if (!m_pluginCallbacks.contains ( pluginId ))
		{
			m_pluginCallbacks.insert ( pluginId, callback );
		}
	}
	// 是否支持插件帧源
	virtual  bool supportPlugin ( QStringView pluginId )const
	{
		return m_pluginCallbacks.contains ( pluginId );
	}
	// 通过插件创建帧源
	virtual FrameSourcePtr createFrameSourceByPlugin ( QStringView pluginId, const FrameSourceDeviceInfo& deviceInfo ) {
		if (!supportPlugin ( pluginId ))
			return nullptr;
		return m_pluginCallbacks.value ( pluginId)( deviceInfo );
	}

protected:
	QHash<QString, FrameSourcePluginCallback> m_pluginCallbacks;	// 帧源插件回调
	
};
using FrameSourceFactoryPtr = std::unique_ptr<FrameSourceFactory>;
