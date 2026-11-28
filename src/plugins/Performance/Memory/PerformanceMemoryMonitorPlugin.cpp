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

QString PerformanceMemoryMonitorPlugin::pluginId () const {
	return "MemoryMonitor";
}
quint64 PerformanceMemoryMonitorPlugin::getTotalMemory ()const {
	return m_monitor->getTotalMemory ();
}
quint64 PerformanceMemoryMonitorPlugin::getTotalGpuMomery ()const {
	return m_monitor->getTotalGpuMemory ();
}

quint64 PerformanceMemoryMonitorPlugin::getAvailableMemory ()const {
	return m_monitor->getAvailableMemory ();
}
quint64 PerformanceMemoryMonitorPlugin::getAvailableGpuMomery ()const {
	return m_monitor->getAvailableGpuMemory ();
}
quint64 PerformanceMemoryMonitorPlugin::getProcessMemory ()const {
	return m_monitor->getProcessMemory ();
}
quint64 PerformanceMemoryMonitorPlugin::getProcessGpuMemory ()const {
	return m_monitor->getProcessGpuMemory ();
}
