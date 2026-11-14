#pragma once
#include "FrameHandle.h"
#include <memory>
#include <opencv2/core.hpp>
#ifdef YOLO_HAVE_CUDA
#include  "CudaDevice.h"
#endif


// 轻量的GPU单图帧句柄(imread获取的CPU帧上传到GPU并进行转换为BGR24)
// 使用cudamalloc获取显存,直接传入到GPU预处理器
class GpuImageFrameHandle : public FrameHandle {
public:
	// 删除器
	struct GpuDataDeleter {
		void operator()( void* ptr ) const {
#ifdef YOLO_HAVE_CUDA
			if (ptr) {
				cudaFree( ptr );
			}
			ptr = nullptr;
#else
			delete ptr;
			ptr = nullptr;
#endif
		}
	};
	using GpuDataPtr = std::unique_ptr<uint8_t, GpuDataDeleter>;
	GpuImageFrameHandle( const cv::Mat& mat, int index );
	~GpuImageFrameHandle() override;

	void setFrameIndex( int index ) override;
	int width() const override;
	int height() const override;
	int channels() const override;
	int stride() const override;
	int frameIndex() const override;
	bool dataEmpty()const;
	PixelFormat pixelFormat() const override;
	PlaneDescriptor plane( int planeIndex ) const override;
	bool isValid() const;
	MemoryType memoryType() const override;

private:
	GpuDataPtr m_gpuData; // GPU显存
	int m_frameIndex = -1;
	int width_ = 0;
	int height_ = 0;
	int channels_ = 0;
	int stride_ = 0;
	PixelFormat pixelFormat_ = PixelFormat::Other;
};
