#pragma once
#include <QtGlobal>
#include <mutex>
#include <wrl/client.h>
#include <dxgi1_4.h>
#include "MemoryMonitor.h"
// Windows内存监控插件
class WinMemoryMonitor: public MemoryMonitor {
public:
	~WinMemoryMonitor () override = default;
	quint64 getTotalMemory ()const override;
	quint64 getTotalGpuMemory ()const override;
	quint64 getAvailableMemory ()const override;
	quint64 getAvailableGpuMemory ()const override;
	quint64 getProcessMemory ()const override;
	quint64 getProcessGpuMemory ()const override;

private: 
	bool ensureGpu ()const;
	mutable std::once_flag m_gpuOnce;   // 保证只初始化一次
	mutable Microsoft::WRL::ComPtr<IDXGIAdapter3> m_adapter;
};