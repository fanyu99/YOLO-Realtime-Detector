#pragma once
#include "IPerformanceMonitor.h"
class NullPerformanceMonitor final: public IPerformanceMonitor {
public:
	inline bool isEnabled() const noexcept override { return false; }
	inline bool isStageEnabled( PerformanceStage stage ) const noexcept override { return false; }	// 阶段都不进行记录
	inline void recordStage( PerformanceStage stage, quint64 nsElapsed ) noexcept override {}
	inline void recordFrameResult( bool success, quint64 frameTotalNs ) noexcept override {}
	inline void setConfig( const PerformanceConfig& config ) noexcept override {}
};
