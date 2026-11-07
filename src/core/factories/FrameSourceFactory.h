#pragma once
#include <QString>
#include <functional>
#include  "DetectionTypes.h"
#include "FrameHandle.h"
#include "FrameSource.h"

enum class CameraTriggerMode {
	Off,      // 关闭触发,连续采集
	Software, // 软件触发
	Line0,    // 硬触发,使用Line0
	Line1,    // 硬触发,使用Line1
	Line2,    // 硬触发,使用Line2
};
// Hik注册器
using HikCreator = std::function<FrameSourcePtr(QStringView serial,CameraTriggerMode mode,MemoryType memoryType)>;

// 抽象帧源工厂类
class FrameSourceFactory {
public:
	virtual ~FrameSourceFactory() = default;
	// 1.创建文件帧源
	virtual FrameSourcePtr createFileFrameSource( QStringView filePath ) = 0;   // 创建文件帧源（图片/视频）
	// 创建摄像头帧源(ffmpeg)
	virtual FrameSourcePtr createCameraFrameSource(const QString& nameOrId, const QString& formatName = QString()) = 0;	

	// 2. Hik注册以及创建帧源
	void setHikCreator( HikCreator creator ) { m_hikCreator = std::move( creator ); }
	bool supportHik() const { return m_hikCreator != nullptr; }
	// 创建帧源（未注册Hik时返回nullptr）
	virtual FrameSourcePtr createHikCameraFrameSource( QStringView serial = {} , CameraTriggerMode mode=CameraTriggerMode::Off, MemoryType memoryType=MemoryType::CPU ) {
		return m_hikCreator ? m_hikCreator( serial , mode , memoryType ) : nullptr;
	}

protected:
	HikCreator m_hikCreator{nullptr};	// Hik注册器
	
};
