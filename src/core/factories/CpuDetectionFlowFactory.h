#pragma once
#include "DetectionFlowFactory.h"
// CPU推理流程工厂
class CpuDetectionFlowFactory final : public DetectionFlowFactory {
public:
	~CpuDetectionFlowFactory() override = default;
	DetectionFlowPtr createFlow( const DetectorConfig& config ) override;
};