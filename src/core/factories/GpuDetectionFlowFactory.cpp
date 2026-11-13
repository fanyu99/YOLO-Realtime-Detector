#include  "GpuDetectionFlowFactory.h"
#include "GpuDetectionFlow.h"
DetectionFlowPtr GpuDetectionFlowFactory::createDetectionFlow( const DetectorConfig& config ) {
	return std::make_unique<GpuDetectionFlow>( config );
}