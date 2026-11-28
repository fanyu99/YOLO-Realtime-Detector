#pragma once
#include <QtPlugin>
#include <QString>
#include <optional>
#include "PerformanceTypes.h"
#include "PluginInterface.h"

inline constexpr auto kPerformanceMemoryMonitorPluginId = "PerformanceMemoryMonitor";

// 内存监控插件接口
class MemoryMonitorPluginInterface : public PluginInterface {
public:
	// 获取内存快照（失败时返回std::nullopt）
	virtual std::optional<MemorySnapshot> getMemorySnapshot ()const = 0;
	virtual ~MemoryMonitorPluginInterface () = default;
};
#define MemoryMonitorPluginInterface_IID "com.yoloqt.MemoryMonitorPluginInterface"
Q_DECLARE_INTERFACE ( MemoryMonitorPluginInterface, MemoryMonitorPluginInterface_IID )
