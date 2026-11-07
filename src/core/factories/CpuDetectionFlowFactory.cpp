#include "CpuDetectionFlowFactory.h"
#include "CpuDetectionFlow.h"
// 创建 CPU 完整处理流程（预处理+推理+后处理）
DetectionFlowPtr CpuDetectionFlowFactory::createDetectionFlow( const DetectorConfig& config ) {
	return std::make_unique<CpuDetectionFlow>(config);
}
