#pragma once	 
#include "FrameSourceFactory.h"
class GpuFrameSourceFactory : public FrameSourceFactory {
public:
	~GpuFrameSourceFactory()override = default;
	FrameSourcePtr createImageFrameSource( QStringView filePath ) override;
	FrameSourcePtr createVideoFrameSource( QStringView filePath, MemoryType memoryType = MemoryType::GPU ) override;
	FrameSourcePtr createCameraFrameSource( const QString& nameOrId, const QString& formatName = QString(), MemoryType memoryType = MemoryType::GPU ) override;
};