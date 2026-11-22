#pragma once
#include "CudaKernelError.h"
#include <cuda_runtime.h>

// CUDAkernel预处理接口
// 使用出参形式返回结果，错误通过CudaKernelError返回

// kernel参数
struct CudaLetterboxParams {
	int srcW = 0;
	int srcH = 0;
	// 模型输入尺寸
	int dstW = 0;
	int dstH = 0;
	int newW = 0;      // 缩放后宽
	int newH = 0;      // 缩放后高
	int padX = 0;      // 左侧 padding 像素
	int padY = 0;      // 顶部 padding 像素
	float scale = 0.0f; // 缩放比例
	// 归一化参数
	float normScale = 1.0f / 255.0f;
	// 目标 buffer（float32 NCHW 1×3×dstH×dstW，device memory，由调用方持有）
	float* dst = nullptr;
};

// 执行kernel预处理
//   yPlane/uvPlane 为 GpuFrameHandle::plane(0/1).data 的显存设备指针（只读）
//   所有权归帧句柄，kernel 不会释放）；yStride/uvStride 为对应 linesize（字节）
bool launchNv12LetterboxKernel(const uint8_t* yPlane, int yStride,
	const uint8_t* uvPlane, int uvStride,
	const CudaLetterboxParams& params,
	cudaStream_t stream,
	CudaKernelError* err = nullptr);

// BGR24 packed格式的letterbox kernel（imread单图上传到显存的帧）
//   src 为 packed BGR24 设备指针（只读，所有权归帧句柄）；stride 为行字节数
//   内存序 BGR → 写出时转换为 RGB 平面序（与 NV12 kernel 输出语义一致，模型输入为 RGB NCHW）
bool launchBgr24LetterboxKernel(const uint8_t* src, int stride,
	const CudaLetterboxParams& params,
	cudaStream_t stream,
	CudaKernelError* err = nullptr);

// ---- 显示用色彩转换 kernel（M3 GPU 叠加层）------------------------------------
// 全分辨率 1:1（无 letterbox、无归一化），输出 BGRA8
// （DXGI_FORMAT_B8G8R8A8_UNORM 布局：byte 序 B,G,R,A）。
// 目标用 cudaSurfaceObject_t 而非裸指针：D3D11 纹理是不透明布局，互操作只能通过
// cudaArray + surface 访问（cudaGraphicsResourceGetMappedPointer 对纹理无效）。

// NVDEC 输出的色彩范围
enum class CudaColorRange {
	Limited,	// Y∈[16,219]（NVDEC/FFmpeg 默认）
	Full		// Y∈[0,255]（源声明为 full/JPEG range 时）
};

// NV12 → BGRA8：Y/UV 平面直取，1:1 nearest 索引（显示路径无缩放）
bool launchNv12ToBgraKernel(const uint8_t* yPlane, int yStride,
	const uint8_t* uvPlane, int uvStride,
	int srcW, int srcH,
	cudaSurfaceObject_t dstSurface,
	CudaColorRange range,
	cudaStream_t stream,
	CudaKernelError* err = nullptr);

// BGR24 → BGRA8：packed 直拷 + alpha 置 255（GpuImageFrameHandle 上传的单图帧）
bool launchBgr24ToBgraKernel(const uint8_t* src, int stride,
	int srcW, int srcH,
	cudaSurfaceObject_t dstSurface,
	cudaStream_t stream,
	CudaKernelError* err = nullptr);
