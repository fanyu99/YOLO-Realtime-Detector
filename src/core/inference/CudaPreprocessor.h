#pragma once
#include <optional>
#include "DetectionTypes.h"
#include "IPerformanceMonitor.h"
#ifdef YOLO_HAVE_CUDA
#include "CudaStageTimer.h"
#endif



// GPU预处理结果
struct GpuPreprocessResult {
	float* gpuData;	// GPU帧数据，存储在GPU中
	LetterboxInfo letterboxInfo; // letterbox处理信息
	// 判断是否有效：
	bool isValid() const { return gpuData && letterboxInfo.isValid(); }
};

// GPU预处理器,直接处理GPU中得到的帧数据
class CudaPreprocessor {
public:
	explicit CudaPreprocessor( IPerformanceMonitor* monitor = nullptr );
	~CudaPreprocessor();
	// 整体的预处理：letterbox + 颜色转换 + NCHW布局,转换错误返回nullopt,成功返回结果
	std::optional<GpuPreprocessResult>preprocess( const FrameHandlePtr& frame, int dstW, int dstH );

	// 读取kernel内部的计时（阻塞）
	void waitForKernelTime();

	const DetectionError& lastError() const;// 获取最近的一次错误




private:
	// 保证输出显存够用（float32 NCHW 1×3×dstH×dstW）
	bool ensureOutputBuffer( size_t sizeEnsure );
	float* m_inputBuffer = nullptr;	  // 输入缓存
	size_t m_bufferSize = 0;
	DetectionError m_lastError{};	// 最近一次错误
#ifdef YOLO_HAVE_CUDA
	CudaStageTimer m_kernelTimer;	// 预处理 kernel 的设备耗时
#endif
};