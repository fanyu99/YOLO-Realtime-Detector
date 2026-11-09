#pragma once
#include <cuda_runtime.h>  
#include <QString>
#include "DetectionTypes.h"

struct AVBufferRef;  // 源文件

// 进程级的单例，因此在创建Worker时创建
class CudaDevice {
public:
	CudaDevice& operator=( const CudaDevice& ) = delete;
    CudaDevice(const CudaDevice&) = delete;
    // 创建单例（进程单例），使用std::call_once
    static CudaDevice& instance();       
    // 初始化设备（需要选择设备），config里面有device_id
    bool init( int deviceId );                // 保证幂等
    bool isValid() const;   // 判断是否有效(deviceId + stream + hwDevice)
    // 借出 FFmpeg hwdevice 的新引用（av_buffer_ref），调用方负责 unref；无效返回 nullptr
    AVBufferRef* borrowHwDevice();
    cudaStream_t stream() const;   // 获取统一的工作流
    void use() const;                       // 在当前的线程中设置设备，Worker 线程启动时调用 / Session创建时调用
    QString describe() const;               // ID+设备名+显存
    QString deviceName() const; // 获取单独的设备名（QML显示）
	DetectionError lastError() const;  // 获取最后一次错误
private:
    CudaDevice() = default;
    ~CudaDevice();
    int m_deviceId = -1;
    cudaStream_t m_stream{};
    AVBufferRef* m_hwDevice = nullptr;  // ffmpeg的硬件设备上下文引用ref，只能借用给别人
    QString m_deviceName;
    DetectionError m_lastError{};  // 最近一次错误
};