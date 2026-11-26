#pragma once
#include "IPerformanceMonitor.h"
class NullPerformanceMonitor final: public IPerformanceMonitor {
public:
	 bool isEnabled() const noexcept override { return false; }
	 bool isStageEnabled( PerformanceStage stage ) const noexcept override { return false; }	// 阶段都不进行记录
	 void recordStage( PerformanceStage stage, quint64 nsElapsed ) noexcept override {}
	 void setWarmupIterations( int iterations ) noexcept override {}
	 void recordFrameResult( bool success, quint64 frameTotalNs ) noexcept override {}
	 void setConfig( const PerformanceConfig& config ) noexcept override {}
	 void setIsRunning ( bool)override{}
	 void flushSnapshot () override {}

};
