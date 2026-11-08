#pragma once
#include "DetectionFlow.h"
// 推理流程抽象工厂
// 具体流程的选择依据 DetectorConfig::effectiveMode()（active_mode 优先，未设置时跟随 preferred_mode）
class DetectionFlowFactory {
public:
	virtual ~DetectionFlowFactory() = default;
	// 根据 config.effectiveMode() 创建对应的推理流程（不支持的模式返回nullptr）
	virtual DetectionFlowPtr createDetectionFlow( const DetectorConfig& config ) = 0;
};
using DetectionFlowFactoryPtr = std::unique_ptr<DetectionFlowFactory>;
