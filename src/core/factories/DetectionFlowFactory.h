#pragma once
#include "DetectionFlow.h"
// 推理流程抽象工厂
class DetectionFlowFactory {
public:
	virtual ~DetectionFlowFactory() = default;
	virtual DetectionFlowPtr createFlow( const DetectorConfig& config ) = 0;
};