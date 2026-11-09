#pragma once
#include "FrameHandle.h"
#include "FfmpegDecoder.h"
// GPU帧句柄：内部存储FFmpeg AVFrame（引用计数持有，零拷贝共享解码器缓冲区）
// NVDEC硬解输出(AV_PIX_FMT_CUDA)时，plane()返回显存中的NV12设备指针，供CUDA预处理器直接访问
class GpuFrameHandle final : public FrameHandle {
public:
	explicit GpuFrameHandle(AVFrame* frame); // av_frame_clone引用计数接管，零拷贝
	GpuFrameHandle() = default;
	~GpuFrameHandle()override;
	void setFrameIndex( int index ) override;
	int width() const override;
	int height() const override;
	int channels() const override; // packed格式返回通道数；planar(NV12/YUV420P)与硬件帧返回0 表示不能按照通道访问
	int stride() const override; // plane0的linesize(pitch)
	int frameIndex() const override;
	PixelFormat pixelFormat() const override; // AV_PIX_FMT_CUDA映射为NV12（显存布局即NV12）
	PlaneDescriptor plane( int planeIndex ) const override; // 硬件帧返回设备指针，需调用方自行绑定CUDA context
	// 获取内部帧只读指针（含hw_frames_ctx，预处理器可从中取CUDA context）
	const AVFrame* constFrame() const;
	// 是否持有有效帧
	bool isValid() const;
	MemoryType memoryType() const override;
private:
	AVFramePtr m_frame{ nullptr };
	int m_frameIndex = -1; // 帧序号
};
