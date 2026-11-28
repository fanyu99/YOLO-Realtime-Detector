#include "LinuxMemoryMonitor.h"

#include <QFile>
#include <QLibrary>
#include <QTextStream>
#include <unistd.h>
#include <vector>

namespace {

constexpr quint64 kBytesPerKb = 1024;
constexpr int kNvmlSuccess = 0;
constexpr unsigned long long kNvmlValueNotAvailable = ~0ull;


quint64 readProcKb ( const QString& path, const char* key )
{
    QFile file ( path );
    if (!file.open ( QIODevice::ReadOnly | QIODevice::Text ))
        return 0;

    QTextStream in ( &file );
    const QString prefix = QLatin1String ( key ) + QLatin1Char ( ':' );
    QString line;
    while (in.readLineInto ( &line )) {
        if (!line.startsWith ( prefix ))
            continue;

        const QString value = line.mid ( prefix.size () ).trimmed ()
            .section ( QLatin1Char ( ' ' ), 0, 0 );
        bool ok = false;
        const quint64 kb = value.toULongLong ( &ok );
        return ok ? kb : 0;
    }
    return 0;
}

quint64 readMeminfoKb ( const char* key )
{
    return readProcKb ( QStringLiteral ( "/proc/meminfo" ), key );
}


struct NvmlMemory {
    unsigned long long total;
    unsigned long long free;
    unsigned long long used;
};

struct NvmlProcessInfo {
    unsigned int pid;
    unsigned long long usedGpuMemory;
};

using NvmlDevice = void*;

struct NvmlApi {
    QLibrary library;
    int ( *init ) () = nullptr;
    int ( *getCount ) ( unsigned int* ) = nullptr;
    int ( *getHandleByIndex ) ( unsigned int, NvmlDevice* ) = nullptr;
    int ( *getMemoryInfo ) ( NvmlDevice, NvmlMemory* ) = nullptr;
    int ( *getComputeRunningProcesses ) ( NvmlDevice, unsigned int*, NvmlProcessInfo* ) = nullptr;
    bool available = false;
};

template <typename Fn>
Fn resolve ( QLibrary& library, const char* name )
{
    return reinterpret_cast<Fn>( library.resolve ( name ) );
}

void loadNvml ( NvmlApi& api )
{
    api.library.setFileName ( QStringLiteral ( "libnvidia-ml.so.1" ) );
    if (!api.library.load ())
        return;

    api.init = resolve<decltype ( api.init )> ( api.library, "nvmlInit_v2" );
    api.getCount = resolve<decltype ( api.getCount )> ( api.library, "nvmlDeviceGetCount_v2" );
    api.getHandleByIndex = resolve<decltype ( api.getHandleByIndex )> ( api.library, "nvmlDeviceGetHandleByIndex_v2" );
    api.getMemoryInfo = resolve<decltype ( api.getMemoryInfo )> ( api.library, "nvmlDeviceGetMemoryInfo" );
    api.getComputeRunningProcesses = resolve<decltype ( api.getComputeRunningProcesses )> (
        api.library, "nvmlDeviceGetComputeRunningProcesses" );

    if (!api.init || !api.getCount || !api.getHandleByIndex || !api.getMemoryInfo)
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

} 

quint64 LinuxMemoryMonitor::getTotalMemory () const
{
    return readMeminfoKb ( "MemTotal" );
}

quint64 LinuxMemoryMonitor::getAvailableMemory () const
{
    const quint64 available = readMeminfoKb ( "MemAvailable" );
    if (available > 0)
        return available;

    return readMeminfoKb ( "MemFree" )
        + readMeminfoKb ( "Buffers" )
        + readMeminfoKb ( "Cached" );
}

quint64 LinuxMemoryMonitor::getProcessMemory () const
{
    return readProcKb ( QStringLiteral ( "/proc/self/status" ), "VmRSS" );
}


bool LinuxMemoryMonitor::ensureGpu () const
{
    std::call_once ( m_gpuOnce, [ this ] {
        NvmlApi& api = nvmlApi ();
        if (!api.available)
            return;

        unsigned int count = 0;
        if (api.getCount ( &count ) != kNvmlSuccess)
            return;

        unsigned long long bestTotal = 0;
        for (unsigned int i = 0; i < count; ++i) {
            NvmlDevice device = nullptr;
            if (api.getHandleByIndex ( i, &device ) != kNvmlSuccess)
                continue;

            NvmlMemory memory{};
            if (api.getMemoryInfo ( device, &memory ) != kNvmlSuccess)
                continue;
            // 取最大显存的GPU
            if (memory.total > bestTotal) {
                bestTotal = memory.total;
                m_device = device;
            }
        }
        } );

    return m_device != nullptr;
}

quint64 LinuxMemoryMonitor::getTotalGpuMemory () const
{
    if (!ensureGpu ())
        return 0;

    NvmlMemory memory{};
    if (nvmlApi ().getMemoryInfo ( m_device, &memory ) != kNvmlSuccess)
        return 0;
    return memory.total / kBytesPerKb;
}

quint64 LinuxMemoryMonitor::getAvailableGpuMemory () const
{
    if (!ensureGpu ())
        return 0;

    NvmlMemory memory{};
    if (nvmlApi ().getMemoryInfo ( m_device, &memory ) != kNvmlSuccess)
        return 0;
    return memory.free / kBytesPerKb;
}

quint64 LinuxMemoryMonitor::getProcessGpuMemory () const
{
    if (!ensureGpu ())
        return 0;

    NvmlApi& api = nvmlApi ();
    if (!api.getComputeRunningProcesses)
        return 0;

    unsigned int count = 0;
    if (api.getComputeRunningProcesses ( m_device, &count, nullptr ) != kNvmlSuccess || count == 0)
        return 0;

    std::vector<NvmlProcessInfo> processes ( count );
    if (api.getComputeRunningProcesses ( m_device, &count, processes.data () ) != kNvmlSuccess)
        return 0;

    const auto self = static_cast<unsigned int>( ::getpid () );
    quint64 used = 0;
    for (unsigned int i = 0; i < count; ++i) {
        if (processes[ i ].pid != self)
            continue;
        if (processes[ i ].usedGpuMemory != kNvmlValueNotAvailable)
            used += processes[ i ].usedGpuMemory;
    }
    return used / kBytesPerKb;
}
