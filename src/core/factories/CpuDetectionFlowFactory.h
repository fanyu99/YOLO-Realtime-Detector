#pragma once
#include "DetectionFlowFactory.h"
// CPU 侧推理流程工厂：只负责创建 CpuDetectionFlow，模式选择由上层根据 config.preferred_mode 决定
class CpuDetectionFlowFactory : public DetectionFlowFactory {
public:
	explicit CpuDetectionFlowFactory() = default;
	~CpuDetectionFlowFactory() override = default;
	DetectionFlowPtr createDetectionFlow( const DetectorConfig& config ) override;
};
