#pragma once
// =============================================================================
// CUDA 预处理 kernel 对外接口（launch 层）
//
// 职责：把一帧 NV12 显存帧（NVDEC 零拷贝路径）在一次 kernel launch 中完成：
//   letterbox 缩放 + 填充 → YUV→RGB 色彩转换 → /255 归一化 → HWC→NCHW
// 输出为 float32 NCHW (1×3×dstH×dstW)，常驻显存，直接可绑 ORT CUDA EP 输入。
// 错误处理：与 CpuPreprocessor 同构——launch 函数返回 bool，
// 失败时通过 err 出参返回 DetectionError（可为 nullptr 表示不关心原因）。
// =============================================================================
#include "DetectionTypes.h"
#include <cuda_runtime.h>

// ---- kernel 参数块 -----------------------------------------------------------
// 主机端计算好几何参数后一次性传入，kernel 内不再做任何几何分支。
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
	float scale = 0.0f; // 缩放比例（仅记录用，kernel 用 newW/newH 做逆映射）
	// 归一化参数
	float normScale = 1.0f / 255.0f;
	// 目标 buffer（float32 NCHW 1×3×dstH×dstW，device memory，由调用方持有）
	float* dst = nullptr;
};

// ---- launch 接口 --------------------------------------------------------------

// NV12 零拷贝路径：
//   yPlane/uvPlane 为 GpuFrameHandle::plane(0/1).data 的显存设备指针（只读借用，
//   所有权归帧句柄，kernel 不会释放）；yStride/uvStride 为对应 linesize（字节）。
//   在 stream 上异步执行；launch 后无需同步，后续 ORT 推理排到同一条 stream
//   即可自然串行化。失败返回 false。
bool launchNv12LetterboxKernel(const uint8_t* yPlane, int yStride,
	const uint8_t* uvPlane, int uvStride,
	const CudaLetterboxParams& params,
	cudaStream_t stream,
	DetectionError* err = nullptr);
