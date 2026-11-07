#include "CpuFrameSourceFactory.h"
#include "FileFrameSource.h"
#include "FfmpegFrameSource.h"
#include "CpuFrameHandle.h"
// 创建CPU帧句柄的文件帧源（图片cv::Mat/视频ffmpeg）
FrameSourcePtr CpuFrameSourceFactory::createFileFrameSource( QStringView filePath ) {
	if (isImagePath( filePath )) {
		// 创建含CPU帧句柄的图片帧源
		return FileFrameSource::forFile( filePath, MemoryType::CPU );
	}
	else if (isVideoPath( filePath )) {
		// 创建含CPU帧句柄的视频帧源
		return FfmpegFrameSource::forLocalFile( filePath, MemoryType::CPU );
	}
	return nullptr;
}
// 创建摄像头帧源(使用ffmpeg帧源)
FrameSourcePtr CpuFrameSourceFactory::createCameraFrameSource( const QString& nameOrId, const QString& formatName ) {
	// 创建含CPU帧句柄的摄像头帧源
	return FfmpegFrameSource::forDevice( nameOrId, formatName, MemoryType::CPU );
}
