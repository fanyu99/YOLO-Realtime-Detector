#include  "GpuDetectionFlowFactory.h"
#include "GpuDetectionFlow.h"
DetectionFlowPtr GpuDetectionFlowFactory::createDetectionFlow( const DetectorConfig& config, IPerformanceMonitor* monitor ) {
	return std::make_unique<GpuDetectionFlow>( config, monitor );
}