#include  "FrameSourceFactory.h"
class CpuFrameSourceFactory : public FrameSourceFactory {
public:
	explicit CpuFrameSourceFactory() = default;
	~CpuFrameSourceFactory() override = default;
	// 1. 创建图片帧源（cv::imread，固定 CPU 内存）
	FrameSourcePtr createImageFrameSource( QStringView filePath ) override;
	// 创建视频帧源（ffmpeg）
	FrameSourcePtr createVideoFrameSource( QStringView filePath, MemoryType memoryType = MemoryType::CPU ) override;
	// 2. 创建摄像头帧源(使用ffmpeg帧源)
	FrameSourcePtr createCameraFrameSource(const QString& nameOrId, const QString& formatName, MemoryType memoryType = MemoryType::CPU) override;

};