#include "CpuFrameSourceFactory.h"
#include "ImageFrameSource.h"
#include "FfmpegFrameSource.h"
#include "CpuFrameHandle.h"
// 创建图片帧源（cv::imread 解码，固定 CPU 内存，忽略 memoryType）
FrameSourcePtr CpuFrameSourceFactory::createImageFrameSource( QStringView filePath ) {
	return ImageFrameSource::forFile( filePath );
}
// 创建视频帧源（含CPU帧句柄的视频帧源）
FrameSourcePtr CpuFrameSourceFactory::createVideoFrameSource( QStringView filePath, MemoryType memoryType ) {
	return FfmpegFrameSource::forLocalFile( filePath, memoryType );
}
// 创建摄像头帧源(使用ffmpeg帧源)
FrameSourcePtr CpuFrameSourceFactory::createCameraFrameSource( const QString& nameOrId, const QString& formatName, MemoryType memoryType ) {
	// 创建含CPU帧句柄的摄像头帧源
	return FfmpegFrameSource::forDevice( nameOrId, formatName, memoryType );
}
