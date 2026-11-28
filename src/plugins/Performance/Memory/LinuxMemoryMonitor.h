#pragma once
#include "MemoryMonitor.h"
#include <mutex>

class LinuxMemoryMonitor final : public MemoryMonitor {
public:
	~LinuxMemoryMonitor () override = default;
	quint64 getTotalMemory ()const override;
	quint64 getTotalGpuMemory ()const override;
	quint64 getAvailableMemory ()const override;
	quint64 getAvailableGpuMemory ()const override;
	quint64 getProcessMemory ()const override;
	quint64 getProcessGpuMemory ()const override;
private:
	bool ensureGpu ()const;
	mutable std::once_flag m_gpuOnce;
	mutable void* m_device = nullptr;   // 设备指针
};
