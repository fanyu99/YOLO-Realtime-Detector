#pragma once
#include "DetectionTypes.h"



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
	~CudaPreprocessor();
	// 整体的预处理：letterbox + 颜色转换 + NCHW布局,转换错误返回nullopt,成功返回结果
	std::optional<GpuPreprocessResult>preprocess( const FrameHandlePtr& frame, int dstW, int dstH );

	const DetectionError& lastError() const;// 获取最近的一次错误




private:
	// 保证输出显存够用（float32 NCHW 1×3×dstH×dstW）
	bool ensureOutputBuffer( size_t sizeEnsure );
	float* m_inputBuffer = nullptr;	  // 输入缓存
	size_t m_bufferSize = 0;
	DetectionError m_lastError{};	// 最近一次错误
};