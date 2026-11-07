#include  "FrameSourceFactory.h"
class CpuFrameSourceFactory : public FrameSourceFactory {
public:
	explicit CpuFrameSourceFactory() = default;
	~CpuFrameSourceFactory() override = default;
	// 1. 创建文件帧源（图片使用cv::Mat 视频使用ffmpeg）
	FrameSourcePtr createFileFrameSource( QStringView filePath, MemoryType memoryType = MemoryType::CPU ) override;
	// 2. 创建摄像头帧源(使用ffmpeg帧源)
	FrameSourcePtr createCameraFrameSource(const QString& nameOrId, const QString& formatName, MemoryType memoryType = MemoryType::CPU) override;

};