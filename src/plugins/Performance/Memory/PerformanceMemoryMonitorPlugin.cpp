#include "PerformanceMemoryMonitorPlugin.h"
#if defined(Q_OS_WIN)
#include "WinMemoryMonitor.h"
#elif defined(Q_OS_LINUX)
#include "LinuxMemoryMonitor.h"
#endif

PerformanceMemoryMonitorPlugin::PerformanceMemoryMonitorPlugin ()
#if defined(Q_OS_WIN)
	: m_monitor ( std::make_unique<WinMemoryMonitor> () )
#elif defined(Q_OS_LINUX)
	: m_monitor ( std::make_unique<LinuxMemoryMonitor> () )
#endif
{
}

std::optional<MemorySnapshot> PerformanceMemoryMonitorPlugin::getMemorySnapshot ()const {
	MemorySnapshot snapshot;
	if (m_monitor)
	{
		snapshot.totalMemory = m_monitor->getTotalMemory ();
		snapshot.availableMemory = m_monitor->getAvailableMemory ();
		snapshot.processMemory = m_monitor->getProcessMemory ();
		snapshot.totalGpuMemory = m_monitor->getTotalGpuMemory ();
		snapshot.availableGpuMemory = m_monitor->getAvailableGpuMemory ();
		snapshot.processGpuMemory = m_monitor->getProcessGpuMemory ();
		return std::make_optional<MemorySnapshot> (snapshot);
	}
	return std::nullopt;
}

QString PerformanceMemoryMonitorPlugin::pluginId () const {
	return QString::fromLatin1 ( kPerformanceMemoryMonitorPluginId );
}
