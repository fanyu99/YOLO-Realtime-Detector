#include "WinMemoryMonitor.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <psapi.h>

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "dxgi.lib")

namespace {

    DXGI_QUERY_VIDEO_MEMORY_INFO querySegment ( IDXGIAdapter3* adapter,
        DXGI_MEMORY_SEGMENT_GROUP group )
    {
        DXGI_QUERY_VIDEO_MEMORY_INFO info{};
        if (adapter)
            adapter->QueryVideoMemoryInfo ( 0, group, &info );  
        return info;
    }

} // namespace


// 内存
quint64 WinMemoryMonitor::getTotalMemory () const
{
    MEMORYSTATUSEX st{};
    st.dwLength = sizeof ( st );
    if (!GlobalMemoryStatusEx ( &st ))
        return 0;
    return st.ullTotalPhys;
}

quint64 WinMemoryMonitor::getAvailableMemory () const
{
    MEMORYSTATUSEX st{};
    st.dwLength = sizeof ( st );
    if (!GlobalMemoryStatusEx ( &st ))
        return 0;
    return st.ullAvailPhys;
}



quint64 WinMemoryMonitor::getProcessMemory () const
{
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (!GetProcessMemoryInfo ( GetCurrentProcess (),
        reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
        sizeof ( pmc ) ))
        return 0;
    return static_cast<quint64>(pmc.WorkingSetSize);
}



bool WinMemoryMonitor::ensureGpu () const   
{
    std::call_once ( m_gpuOnce, [ this ] {
        Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
        if (FAILED ( CreateDXGIFactory1 ( IID_PPV_ARGS ( &factory ) ) ))
            return;

        Microsoft::WRL::ComPtr<IDXGIAdapter1> best;
        SIZE_T bestVram = 0;

        for (UINT i = 0; ; ++i) {
            Microsoft::WRL::ComPtr<IDXGIAdapter1> cur;
            if (factory->EnumAdapters1 ( i, &cur ) == DXGI_ERROR_NOT_FOUND)
                break;
            if (!cur)
                continue;

            DXGI_ADAPTER_DESC1 desc{};
            if (FAILED ( cur->GetDesc1 ( &desc ) ))
                continue;
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)   
                continue;

            if (desc.DedicatedVideoMemory > bestVram) {
                bestVram = desc.DedicatedVideoMemory;
                best = cur;
            }
        }

        if (best)
            best.As ( &m_adapter );   
        } );

    return m_adapter != nullptr;
}

quint64 WinMemoryMonitor::getTotalGpuMemory () const
{
    if (!ensureGpu ())
        return 0;

    DXGI_ADAPTER_DESC1 desc{};
    if (FAILED ( m_adapter->GetDesc1 ( &desc ) ))
        return 0;

    return desc.DedicatedVideoMemory > 0 ? desc.DedicatedVideoMemory
        : desc.SharedSystemMemory;
}

quint64 WinMemoryMonitor::getAvailableGpuMemory () const
{
    if (!ensureGpu ())
        return 0;

    const auto info = querySegment ( m_adapter.Get (), DXGI_MEMORY_SEGMENT_GROUP_LOCAL );
    return info.Budget > info.CurrentUsage ? info.Budget - info.CurrentUsage : 0;
}

quint64 WinMemoryMonitor::getProcessGpuMemory () const
{
    if (!ensureGpu ())
        return 0;

    const auto local = querySegment ( m_adapter.Get (), DXGI_MEMORY_SEGMENT_GROUP_LOCAL );
    const auto nonLocal = querySegment ( m_adapter.Get (), DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL );
    return local.CurrentUsage + nonLocal.CurrentUsage;
}