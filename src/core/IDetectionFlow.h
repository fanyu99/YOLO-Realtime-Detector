#pragma once
#include "FrameHandle.h"
#include "DetectionTypes.h"

// 抽象类：处理推理流程的结构
class IDetectionFlow {
public:
	IDetectionFlow() = default;
	virtual ~IDetectionFlow() = default;
	// 执行完整的处理流程：预处理 + 推理 + 后处理
	// 一旦触发fallback(不支持GPU)，销毁当前的流程、重新创建CPU DetectionFlow并执行CPU流程
	virtual bool run( const FrameHandlePtr& handle, DetectionResult& result ) = 0;
	virtual bool warmup( int iterations ) = 0;	// 预热，执行若干次推理以加载模型和初始化资源
	virtual FlowMode activeMode()const = 0;	// 当前流程的实际计算模式：CPU/GPU
	virtual FlowMode preferredMode()const { return FlowMode::CPU; }	// 首选模式：CPU/GPU
};
