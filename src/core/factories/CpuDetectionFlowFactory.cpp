
#include "CpuDetectionFlowFactory.h"
#include "CpuDetectionFlow.h"
DetectionFlowPtr CpuDetectionFlowFactory::createFlow( const DetectorConfig& config ) {
	return std::make_unique<CpuDetectionFlow>( config );
}

