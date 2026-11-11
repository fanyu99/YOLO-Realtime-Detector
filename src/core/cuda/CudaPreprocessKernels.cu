// =============================================================================
// CUDA 预处理 kernel：letterbox + 色彩转换 + 归一化 + NCHW，一次 launch 融合完成
//
// 输入：NV12 显存帧（NVDEC 零拷贝，Y 平面 + 交错 UV 平面设备指针）
//
// 几何约定与 CpuPreprocessor::letterbox 完全同构：
//   有效区 [padX, padX+newW) × [padY, padY+newH)，其余写 114/255，
//   有效区内做 bilinear 逆映射（像素中心对齐，与 cv::INTER_LINEAR 语义一致）。
// =============================================================================
#include "CudaPreprocessKernels.h"
#include <math.h>

// ---- 常量 -------------------------------------------------------------------
constexpr int kBlockDim = 16; // block 16×16，每个线程负责输出图的一个像素

// letterbox 填充色 114（与 CpuPreprocessor 的 cv::Scalar(114,114,114) 对齐），预除以 255
constexpr float kPadValue = 114.0f / 255.0f;

// ---- 设备端工具 ---------------------------------------------------------------

__device__ inline float clampf(float v, float lo, float hi) {
	return fminf(fmaxf(v, lo), hi);
}

// NV12 bilinear 采样 + YUV→RGB（BT.601 limited range，NVDEC 输出的标准 range）。
// (fx, fy) 为输出像素中心在源图上的浮点坐标，可为负/越界，内部 clamp 处理。
__device__ inline void sampleNv12Bilinear(
	const uint8_t* yPlane, int yStride,
	const uint8_t* uvPlane, int uvStride,
	float fx, float fy, int srcW, int srcH,
	float& r, float& g, float& b)
{
	// ---- Y 平面 bilinear ----
	const int x0i = (int)floorf(fx);
	const int y0i = (int)floorf(fy);
	const float dx = fx - x0i;
	const float dy = fy - y0i;
	const int xc = min(max(x0i, 0), srcW - 1);
	const int yc = min(max(y0i, 0), srcH - 1);
	const int x1i = min(x0i + 1, srcW - 1);
	const int y1i = min(y0i + 1, srcH - 1);

	const uint8_t* rowY0 = yPlane + (size_t)yc * yStride;
	const uint8_t* rowY1 = yPlane + (size_t)y1i * yStride;
	const float y00 = rowY0[xc], y01 = rowY0[x1i];
	const float y10 = rowY1[xc], y11 = rowY1[x1i];
	const float yVal = (y00 * (1.0f - dx) + y01 * dx) * (1.0f - dy)
		+ (y10 * (1.0f - dx) + y11 * dx) * dy;

	// ---- UV 平面 bilinear（半分辨率：UV 坐标 = 像素坐标 / 2）----
	const float uFx = fx * 0.5f;
	const float uFy = fy * 0.5f;
	const int uX0i = (int)floorf(uFx);
	const int uY0i = (int)floorf(uFy);
	const float uDx = uFx - uX0i;
	const float uDy = uFy - uY0i;
	const int halfW = (srcW + 1) / 2;
	const int halfH = max(srcH / 2, 1); // 防御 srcH==1 的极端输入（UV 平面至少 1 行）
	const int uXc = min(max(uX0i, 0), halfW - 1);
	const int uYc = min(max(uY0i, 0), halfH - 1);
	const int uX1i = min(uX0i + 1, halfW - 1);
	const int uY1i = min(uY0i + 1, halfH - 1);

	const uint8_t* rowUv0 = uvPlane + (size_t)uYc * uvStride;
	const uint8_t* rowUv1 = uvPlane + (size_t)uY1i * uvStride;
	// NV12 interleaved：每个宏像素 2 字节 [U, V]，偏移 = uIndex * 2
	const float u00 = rowUv0[uXc * 2 + 0], v00 = rowUv0[uXc * 2 + 1];
	const float u01 = rowUv0[uX1i * 2 + 0], v01 = rowUv0[uX1i * 2 + 1];
	const float u10 = rowUv1[uXc * 2 + 0], v10 = rowUv1[uXc * 2 + 1];
	const float u11 = rowUv1[uX1i * 2 + 0], v11 = rowUv1[uX1i * 2 + 1];
	const float uVal = (u00 * (1.0f - uDx) + u01 * uDx) * (1.0f - uDy)
		+ (u10 * (1.0f - uDx) + u11 * uDx) * uDy;
	const float vVal = (v00 * (1.0f - uDx) + v01 * uDx) * (1.0f - uDy)
		+ (v10 * (1.0f - uDx) + v11 * uDx) * uDy;

	// ---- YUV→RGB，BT.601 limited range：Y∈[16,219] → [0,255]，UV offset 128 ----
	const float yy = clampf(1.164f * (yVal - 16.0f), 0.0f, 255.0f);
	const float uu = uVal - 128.0f;
	const float vv = vVal - 128.0f;
	r = clampf(yy + 1.596f * vv, 0.0f, 255.0f);
	g = clampf(yy - 0.391f * uu - 0.806f * vv, 0.0f, 255.0f);
	b = clampf(yy + 2.018f * uu, 0.0f, 255.0f);
}

// ---- 融合 kernel：每个线程处理输出 blob 的一个 (x, y) 像素 --------------------
__global__ void letterboxKernelNv12(
	const uint8_t* yPlane, const uint8_t* uvPlane,
	int yStride, int uvStride,
	int srcW, int srcH,
	int dstW, int dstH, int newW, int newH, int padX, int padY,
	float normScale, float* dst)
{
	const int x = blockIdx.x * blockDim.x + threadIdx.x;
	const int y = blockIdx.y * blockDim.y + threadIdx.y;
	if (x >= dstW || y >= dstH)
		return;

	// NCHW：channel 平面连续，先定位 C 平面再写 W 内偏移
	const int planeSize = dstW * dstH;

	// letterbox padding 区域：直接写 114/255
	if (x < padX || y < padY || x >= padX + newW || y >= padY + newH) {
		dst[0 * planeSize + y * dstW + x] = kPadValue;
		dst[1 * planeSize + y * dstW + x] = kPadValue;
		dst[2 * planeSize + y * dstW + x] = kPadValue;
		return;
	}

	// 有效区：输出坐标 → 源坐标逆映射（像素中心对齐，与 cv::INTER_LINEAR 一致）
	const float fx = (x - padX + 0.5f) * ((float)srcW / (float)newW) - 0.5f;
	const float fy = (y - padY + 0.5f) * ((float)srcH / (float)newH) - 0.5f;

	float r, g, b;
	sampleNv12Bilinear(yPlane, yStride, uvPlane, uvStride, fx, fy, srcW, srcH, r, g, b);

	// RGB → NCHW：R/G/B 三个平面 + /255 归一化
	const int idx = y * dstW + x;
	dst[0 * planeSize + idx] = r * normScale;
	dst[1 * planeSize + idx] = g * normScale;
	dst[2 * planeSize + idx] = b * normScale;
}

// ---- 主机端 launcher ----------------------------------------------------------

// 统一的 launch 后错误检查：捕获配置错误（如非法参数/资源不足）
static bool checkLaunchError(const char* who, DetectionError* err) {
	const cudaError_t launchErr = cudaGetLastError();
	if (launchErr != cudaSuccess) {
		if (err) {
			DetectionError e;
			e.errorMessage = QStringLiteral("[%1]: kernel launch 失败: %2")
				.arg(QString::fromLocal8Bit(who))
				.arg(QString::fromLocal8Bit(cudaGetErrorString(launchErr)));
			e.errorCategory = DetectionErrorCategory::CUDA;
			e.errorCode = DetectionErrorCode::PreprocessFailed;
			*err = e;
		}
		return false;
	}
	return true;
}

// 统一的参数校验
static bool validateParams(const CudaLetterboxParams& p, DetectionError* err) {
	if (!p.dst || p.srcW <= 0 || p.srcH <= 0 || p.dstW <= 0 || p.dstH <= 0
		|| p.newW <= 0 || p.newH <= 0
		|| p.padX < 0 || p.padY < 0
		|| p.padX + p.newW > p.dstW || p.padY + p.newH > p.dstH) {
		if (err) {
			DetectionError e;
			e.errorMessage = QStringLiteral("[CudaPreprocessKernels]: letterbox 参数非法: ")
				+ QStringLiteral("src=%1x%2 dst=%3x%4 new=%5x%6 pad=%7,%8")
					.arg(p.srcW).arg(p.srcH).arg(p.dstW).arg(p.dstH)
					.arg(p.newW).arg(p.newH).arg(p.padX).arg(p.padY);
			e.errorCategory = DetectionErrorCategory::Preprocess;
			e.errorCode = DetectionErrorCode::PreprocessFailed;
			*err = e;
		}
		return false;
	}
	return true;
}

bool launchNv12LetterboxKernel(const uint8_t* yPlane, int yStride,
	const uint8_t* uvPlane, int uvStride,
	const CudaLetterboxParams& params,
	cudaStream_t stream,
	DetectionError* err)
{
	if (!yPlane || !uvPlane || yStride <= 0 || uvStride <= 0) {
		if (err) {
			DetectionError e;
			e.errorMessage = QStringLiteral("[CudaPreprocessKernels]: NV12 设备指针/stride 无效");
			e.errorCategory = DetectionErrorCategory::Preprocess;
			e.errorCode = DetectionErrorCode::PreprocessFailed;
			*err = e;
		}
		return false;
	}
	if (!validateParams(params, err))
		return false;

	const dim3 block(kBlockDim, kBlockDim);
	const dim3 grid((params.dstW + kBlockDim - 1) / kBlockDim,
		(params.dstH + kBlockDim - 1) / kBlockDim);

	letterboxKernelNv12<<<grid, block, 0, stream>>>(
		yPlane, uvPlane, yStride, uvStride,
		params.srcW, params.srcH,
		params.dstW, params.dstH, params.newW, params.newH, params.padX, params.padY,
		params.normScale, params.dst);

	return checkLaunchError("CudaPreprocessKernels", err);
}
