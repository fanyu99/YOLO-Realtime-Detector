#include "PerformanceMemoryMonitorPlugin.h"
#if WIN32
#include "WinMemoryMonitor.h"


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