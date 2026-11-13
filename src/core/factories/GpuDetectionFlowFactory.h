#pragma once
#include "DetectionFlowFactory.h"

class GpuDetectionFlowFactory : public DetectionFlowFactory {
public:
	explicit  GpuDetectionFlowFactory() = default;
	~GpuDetectionFlowFactory() override = default;
	DetectionFlowPtr createDetectionFlow( const DetectorConfig& config ) override;
};