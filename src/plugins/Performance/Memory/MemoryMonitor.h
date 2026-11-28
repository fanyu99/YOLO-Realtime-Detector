#pragma once
#include <QtGlobal>
// 抽象接口
// 所有内存/显存返回值单位统一为 KB
class MemoryMonitor {
public:
	virtual ~MemoryMonitor () = default;
	virtual quint64 getTotalMemory ()const=0;
	virtual quint64 getTotalGpuMemory ()const=0;
	virtual quint64 getAvailableMemory ()const=0;
	virtual quint64 getAvailableGpuMemory ()const=0;
	virtual quint64 getProcessMemory ()const=0;
	virtual quint64 getProcessGpuMemory ()const=0;
};