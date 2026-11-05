#pragma once
#include "IDetectionFlow.h"
#include "CpuDetector.h"
#include "CpuPreprocessor.h"
#include "CommonPostProcessor.h"

class CpuDetectionFlow final: public IDetectionFlow {
public:
	CpuDetectionFlow() = default;
	explicit CpuDetectionFlow( const DetectorConfig& config );
	~CpuDetectionFlow() override = default;
	// 执行完整的CPU处理流程：预处理 + 推理
	// 成功返回true,失败返回false，并在result中回填错误信息
	bool run( const FrameHandlePtr& handle, DetectionResult& outResult ) override;
	// 预热，执行若干次推理以加载模型和初始化资源
	bool warmup( int iterations ) override;
	// 当前流程的计算模式：CPU
	ActiveMode activeMode()const override;
private:
	CpuDetector m_detector; // 推理器
	CpuPreprocessor m_preprocessor;	// CPU 预处理器
	CommonPostProcessor m_postprocessor; // 通用后处理器
};
