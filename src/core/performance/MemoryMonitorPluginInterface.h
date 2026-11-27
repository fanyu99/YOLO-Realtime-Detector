#pragma once
#include <QtPlugin>
#include <QString>
#include <memory>
// 内存监控插件接口
class MemoryMonitorPluginInterface {
public:
	using MonitorPtr = std::unique_ptr<MemoryMonitorPluginInterface>;
	virtual QString pluginId () const = 0;
	// 获取总内存大小
	virtual quint64 getTotalMemory ()const = 0;
	virtual quint64 getTotalGpuMomery ()const = 0;
	// 获取可用内存大小
	virtual quint64 getAvailableMemory ()const = 0;
	virtual quint64 getAvailableGpuMomery ()const = 0;
	// 获取当前进程内存使用
	virtual quint64 getProcessMemory ()const = 0;
	virtual quint64 getProcessGpuMemory ()const = 0;
	virtual ~MemoryMonitorPluginInterface () = default;
};
#define MemoryMonitorPluginInterface_IID "com.yoloqt.MemoryMonitorPluginInterface"
Q_DECLARE_INTERFACE ( MemoryMonitorPluginInterface, MemoryMonitorPluginInterface_IID )