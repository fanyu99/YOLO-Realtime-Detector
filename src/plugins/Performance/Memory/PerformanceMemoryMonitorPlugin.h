#pragma once
#include "MemoryMonitorPluginInterface.h"
#include "MemoryMonitor.h"
#include <memory>
class PerformanceMemoryMonitorPlugin final : public QObject, public MemoryMonitorPluginInterface {
	Q_OBJECT
		Q_PLUGIN_METADATA ( IID MemoryMonitorPluginInterface_IID )
		Q_INTERFACES ( PluginInterface MemoryMonitorPluginInterface )
public:
	PerformanceMemoryMonitorPlugin ();
	~PerformanceMemoryMonitorPlugin () override = default;
	std::optional<MemorySnapshot> getMemorySnapshot ()const override;
	QString pluginId () const override;
private:
	std::unique_ptr<MemoryMonitor> m_monitor;
};