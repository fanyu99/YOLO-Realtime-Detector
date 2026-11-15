#pragma once
#include "IPerformanceMonitor.h"
class NullPerformanceMonitor final: public IPerformanceMonitor {
public:
	inline bool isEnabled() const noexcept override { return false; }
	inline void recordStage( PerformanceStage stage, quint64 nsElapsed ) noexcept override {}
	inline void recordFrameResult( bool success ) noexcept override {}
};