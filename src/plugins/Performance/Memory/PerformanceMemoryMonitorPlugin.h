#pragma once
#include "MemoryMonitorPluginInterface.h"
#include "MemoryMonitor.h"
#include <memory>
class PerformanceMemoryMonitorPlugin final : public QObject, public MemoryMonitorPluginInterface {
	Q_OBJECT
		Q_PLUGIN_METADATA ( IID "com.yoloqt.MemoryMonitorPluginInterface" )
		Q_INTERFACES ( MemoryMonitorPluginInterface )
public:
	QString pluginId () const override;
	quint64 getTotalMemory ()const override;
	quint64 getTotalGpuMomery ()const override;
	quint64 getAvailableMemory ()const override;
	quint64 getAvailableGpuMomery ()const override;
	quint64 getProcessMemory ()const override;
	quint64 getProcessGpuMemory ()const override;
private:
	std::unique_ptr<MemoryMonitor> m_monitor;
};