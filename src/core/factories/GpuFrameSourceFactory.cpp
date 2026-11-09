#include "GpuFrameSourceFactory.h"
#include "ImageFrameSource.h"
#include "FfmpegFrameSource.h"
#include <memory>
#include <opencv2/core.hpp>
FrameSourcePtr GpuFrameSourceFactory::createImageFrameSource( QStringView filePath ) {
	return ImageFrameSource::forFile(filePath);
}

FrameSourcePtr GpuFrameSourceFactory::createVideoFrameSource( QStringView filePath, MemoryType memoryType ) {
	return FfmpegFrameSource::forLocalFile( filePath, memoryType );
	
}


FrameSourcePtr GpuFrameSourceFactory::createCameraFrameSource( const QString& nameOrId, const QString& formatName, MemoryType memoryType ) {
	return FfmpegFrameSource::forDevice( nameOrId, formatName, memoryType );
}