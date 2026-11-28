#include "WinMemoryMonitor.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <psapi.h>

#include <QLibrary>
#include <cstring>
#include <optional>

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "dxgi.lib")

namespace {

    constexpr quint64 kBytesPerKb = 1024;
    constexpr int kNvmlSuccess = 0;
    constexpr int kCudaSuccess = 0;

    quint64 bytesToKb ( quint64 bytes )
    {
        return bytes / kBytesPerKb;
    }

    DXGI_QUERY_VIDEO_MEMORY_INFO querySegment ( IDXGIAdapter3* adapter,
        DXGI_MEMORY_SEGMENT_GROUP group )
    {
        DXGI_QUERY_VIDEO_MEMORY_INFO info{};
        if (adapter)
            adapter->QueryVideoMemoryInfo ( 0, group, &info );
        return info;
    }

    struct NvmlMemory {
        unsigned long long total;
        unsigned long long free;
        unsigned long long used;
    };

    using NvmlDevice = void*;

    struct NvmlApi {
        QLibrary library;
        int ( *init ) () = nullptr;
        int ( *getHandleByIndex ) ( unsigned int, NvmlDevice* ) = nullptr;
        int ( *getMemoryInfo ) ( NvmlDevice, NvmlMemory* ) = nullptr;
        bool available = false;
    };

    // 把 CUDA 设备号映射成 DXGI 适配器 LUID
    struct CudaDriverApi {
        QLibrary library;
        int ( *init ) ( unsigned int ) = nullptr;
        int ( *deviceGet ) ( int*, int ) = nullptr;
        int ( *deviceGetLuid ) ( char*, unsigned int*, int ) = nullptr;
        bool available = false;
    };

    template <typename Fn>
    Fn resolve ( QLibrary& library, const char* name )
    {
        return reinterpret_cast<Fn>( library.resolve ( name ) );
    }

    void loadNvml ( NvmlApi& api )
    {
        api.library.setFileName ( QStringLiteral ( "nvml.dll" ) );
        if (!api.library.load ())
            return;

        api.init = resolve<decltype ( api.init )> ( api.library, "nvmlInit_v2" );
        api.getHandleByIndex = resolve<decltype ( api.getHandleByIndex )> (
            api.library, "nvmlDeviceGetHandleByIndex_v2" );
        api.getMemoryInfo = resolve<decltype ( api.getMemoryInfo )> (
            api.library, "nvmlDeviceGetMemoryInfo" );

        if (!api.init || !api.getHandleByIndex || !api.getMemoryInfo)
            return;
        if (api.init () != kNvmlSuccess)
            return;

        api.available = true;
    }

    NvmlApi& nvmlApi ()
    {
        static NvmlApi api;
        static std::once_flag once;
        std::call_once ( once, [] { loadNvml ( api ); } );
        return api;
    }

    // 整卡显存（所有进程合计），不可用返回 nullopt
    std::optional<NvmlMemory> deviceMemory ()
    {
        NvmlApi& api = nvmlApi ();
        if (!api.available)
            return std::nullopt;

        NvmlDevice device = nullptr;
        if (api.getHandleByIndex ( 0, &device ) != kNvmlSuccess)
            return std::nullopt;

        NvmlMemory memory{};
        if (api.getMemoryInfo ( device, &memory ) != kNvmlSuccess)
            return std::nullopt;
        return memory;
    }

    void loadCudaDriver ( CudaDriverApi& api )
    {
        api.library.setFileName ( QStringLiteral ( "nvcuda.dll" ) );
        if (!api.library.load ())
            return;

        api.init = resolve<decltype ( api.init )> ( api.library, "cuInit" );
        api.deviceGet = resolve<decltype ( api.deviceGet )> ( api.library, "cuDeviceGet" );
        api.deviceGetLuid = resolve<decltype ( api.deviceGetLuid )> (
            api.library, "cuDeviceGetLuid" );
        api.available = api.init && api.deviceGet && api.deviceGetLuid;
    }

    CudaDriverApi& cudaDriverApi ()
    {
        static CudaDriverApi api;
        static std::once_flag once;
        std::call_once ( once, [] { loadCudaDriver ( api ); } );
        return api;
    }

    // 推理使用的 CUDA 设备 LUID
    std::optional<LUID> cudaDeviceLuid ()
    {
        static const std::optional<LUID> luid = [] () -> std::optional<LUID> {
            CudaDriverApi& api = cudaDriverApi();
            if (!api.available || api.init ( 0 ) != kCudaSuccess)
                return std::nullopt;

            int device = 0;
            if (api.deviceGet ( &device, 0 ) != kCudaSuccess)
                return std::nullopt;

            char bytes[ sizeof ( LUID ) ] = {};
            unsigned int nodeMask = 0;
            if (api.deviceGetLuid ( bytes, &nodeMask, device ) != kCudaSuccess)
                return std::nullopt;

            LUID result{};
            std::memcpy ( &result, bytes, sizeof ( LUID ) );
            return result;
        }();
        return luid;
    }

} 


// 内存
quint64 WinMemoryMonitor::getTotalMemory () const
{
    MEMORYSTATUSEX st{};
    st.dwLength = sizeof ( st );
    if (!GlobalMemoryStatusEx ( &st ))
        return 0;
    return bytesToKb ( st.ullTotalPhys );
}

quint64 WinMemoryMonitor::getAvailableMemory () const
{
    MEMORYSTATUSEX st{};
    st.dwLength = sizeof ( st );
    if (!GlobalMemoryStatusEx ( &st ))
        return 0;
    return bytesToKb ( st.ullAvailPhys );
}



quint64 WinMemoryMonitor::getProcessMemory () const
{
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (!GetProcessMemoryInfo ( GetCurrentProcess (),
        reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
        sizeof ( pmc ) ))
        return 0;
    return bytesToKb ( static_cast<quint64>(pmc.WorkingSetSize) );
}



bool WinMemoryMonitor::ensureGpu () const
{
    std::call_once ( m_gpuOnce, [ this ] {
        Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
        if (FAILED ( CreateDXGIFactory1 ( IID_PPV_ARGS ( &factory ) ) ))
            return;

        const auto luid = cudaDeviceLuid ();

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

            // 推理设备优先按 CUDA 设备 LUID 精确匹配；"显存最大"在多 GPU 时不一定就是推理设备
            const bool luidMatch = luid
                && desc.AdapterLuid.HighPart == luid->HighPart
                && desc.AdapterLuid.LowPart == luid->LowPart;
            if (luidMatch) {
                best = cur;
                break;
            }

            // 核显上报的 DedicatedVideoMemory 常等于共享内存，不能参与"最大显存"比较
            if (desc.DedicatedVideoMemory == desc.SharedSystemMemory)
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
    // 整卡总显存，与 Linux 端 NVML 语义一致
    if (const auto memory = deviceMemory ())
        return bytesToKb ( memory->total );

    if (!ensureGpu ())
        return 0;

    // 没有 NVML（非 NVIDIA）时退回 DXGI 适配器容量
    DXGI_ADAPTER_DESC1 desc{};
    if (FAILED ( m_adapter->GetDesc1 ( &desc ) ))
        return 0;

    return bytesToKb ( desc.DedicatedVideoMemory > 0 ? desc.DedicatedVideoMemory
        : desc.SharedSystemMemory );
}

quint64 WinMemoryMonitor::getAvailableGpuMemory () const
{
    // 整卡空闲显存需要 NVML；DXGI 只提供进程级预算，拿不到整卡空闲
    if (const auto memory = deviceMemory ())
        return bytesToKb ( memory->free );
    return 0;
}

quint64 WinMemoryMonitor::getProcessGpuMemory () const
{
    if (!ensureGpu ())
        return 0;

    // DXGI 的 CurrentUsage 是进程级统计，包含 CUDA 分配；NVML 在 WDDM 下取不到进程显存
    const auto local = querySegment ( m_adapter.Get (), DXGI_MEMORY_SEGMENT_GROUP_LOCAL );
    const auto nonLocal = querySegment ( m_adapter.Get (), DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL );
    return bytesToKb ( local.CurrentUsage + nonLocal.CurrentUsage );
}
