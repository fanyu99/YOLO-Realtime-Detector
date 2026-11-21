#pragma once
#include <QElapsedTimer>
#include <QString>
#include "PerformanceTypes.h"

// 性能监控接口
// isEnabled / recordStage / recordFrameResult / isStageEnabled	随便调用
// 配置更新、snapshotReady信号 仅在监测器所属线程调用
class IPerformanceMonitor {
public:
	virtual ~IPerformanceMonitor() = default;
	virtual bool isEnabled() const noexcept = 0;	  // 返回是否全局启用性能监控器
	// 返回该阶段是否在当前监控级别下允许记录（monitor内部依据级别掩码过滤）
	virtual bool isStageEnabled( PerformanceStage stage ) const noexcept = 0;
	virtual void recordStage( PerformanceStage stage, quint64 nsElapsed ) noexcept = 0;		  // 记录阶段耗时，单位为ns
	// 记录一帧的结果与广义端到端耗时(读帧到显示/保存完成的耗时)
	virtual void recordFrameResult( bool success, quint64 frameTotalNs ) noexcept = 0;
	virtual void setConfig(const PerformanceConfig& config) noexcept = 0;
	virtual  void setIsRunning ( bool running ) = 0;
};
