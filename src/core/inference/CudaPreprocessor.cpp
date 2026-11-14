#include <memory>
#include "CudaPreprocessor.h"
#include "GpuFrameHandle.h"
#include "GpuImageFrameHandle.h"
#ifdef YOLO_HAVE_CUDA
#include "CudaPreprocessKernels.h"
#include "CudaDevice.h"
#endif

CudaPreprocessor::~CudaPreprocessor() {
#ifdef	YOLO_HAVE_CUDA
	if (m_inputBuffer) {
		cudaFree( m_inputBuffer );
		m_inputBuffer = nullptr;
	}
#endif
	m_bufferSize = 0;
}

const DetectionError& CudaPreprocessor::lastError() const {
	return m_lastError;
}

// 保证显存
bool CudaPreprocessor::ensureOutputBuffer( size_t sizeEnsure ) {
	if (m_inputBuffer && m_bufferSize >= sizeEnsure)
		return true;
	// 先释放显存后重新分配显存
	if (m_inputBuffer) {
		cudaFree( m_inputBuffer );
		m_inputBuffer = nullptr;
		m_bufferSize = 0;
	}
	if (cudaMalloc( &m_inputBuffer, sizeEnsure ) != cudaSuccess) {
		m_inputBuffer = nullptr;
		m_lastError = DetectionError(
			QStringLiteral( "[CudaPreprocessor]: 分配输出blob显存失败: " )
			+ QString::fromLocal8Bit( cudaGetErrorString( cudaGetLastError() ) ),
			DetectionErrorCategory::CUDA,
			DetectionErrorCode::PreprocessFailed );
		return false;
	}
	m_bufferSize = sizeEnsure;
	return true;
}

std::optional<GpuPreprocessResult>
CudaPreprocessor::preprocess( const FrameHandlePtr& frame, int dstW, int dstH ) {
	m_lastError = DetectionError(); // 复位
	if (!frame) {
		m_lastError = DetectionError(
			QStringLiteral( "[CudaPreprocessor]: 输入帧句柄为空" ),
			DetectionErrorCategory::Preprocess,
			DetectionErrorCode::InvalidImage );
		return std::nullopt;
	}
	// GPU帧 ：NV12 或 BGR24
	const bool isNv12 = frame->memoryType() == MemoryType::GPU && frame->pixelFormat() == PixelFormat::NV12;
	const bool isBgr24 = frame->memoryType() == MemoryType::GPU && frame->pixelFormat() == PixelFormat::BGR24;
	if (!isNv12 && !isBgr24) {
		m_lastError = DetectionError(
			QStringLiteral( "[CudaPreprocessor]: 不支持的输入帧: memoryType=" )
			+ (frame->memoryType() == MemoryType::GPU ? QStringLiteral( "GPU" ) : QStringLiteral( "CPU" ))
			+ QStringLiteral( ", pixelFormat=" ) + pixelFormatToString( frame->pixelFormat() )
			+ QStringLiteral( "，仅支持GPU显存帧 + NV12/BGR24" ),
			DetectionErrorCategory::Preprocess,
			DetectionErrorCode::InvalidImage );
		return std::nullopt;
	}
	// NV12: plane0=Y平面 plane1=UV平面
	//BGR24: plane0=packed BGR
	PlaneDescriptor plane0, plane1; 
	if (isNv12) {
		const auto data = std::dynamic_pointer_cast<GpuFrameHandle>( frame );
		if (!data || !data->isValid()) {
			m_lastError = DetectionError( QStringLiteral( "[CudaPreprocessor]: 无法转换为GpuFrameHandle: pixelFormat=" ) + pixelFormatToString( frame->pixelFormat() ), DetectionErrorCategory::Preprocess, DetectionErrorCode::InvalidImage );
			return std::nullopt;
		}
		plane0 = data->plane( 0 );
		plane1 = data->plane( 1 );
	}
	else {
		const auto data = std::dynamic_pointer_cast<GpuImageFrameHandle>( frame );
		if (!data || !data->isValid()) {
			m_lastError = DetectionError( QStringLiteral( "[CudaPreprocessor]: 无法转换为GpuImageFrameHandle: pixelFormat=" ) + pixelFormatToString( frame->pixelFormat() ), DetectionErrorCategory::Preprocess, DetectionErrorCode::InvalidImage );
			return std::nullopt;
		}
		plane0 = data->plane( 0 );
	}
#ifdef YOLO_HAVE_CUDA
	// 设备与工作流检查（预处理kernel与后续ORT推理共用同一条stream）
	const auto& device = CudaDevice::instance();
	if (!device.isValid()) {
		m_lastError = DetectionError( QStringLiteral( "[CudaPreprocessor]: CUDA设备未初始化" ), DetectionErrorCategory::CUDA, DetectionErrorCode::FallbackRequested );
		return std::nullopt;
	}
	device.use();  // 使用该设备
#endif
	if (dstH <= 0 || dstW <= 0) {
		m_lastError = DetectionError( QStringLiteral( "[CudaPreprocessor]: 目标尺寸非法: H:" ) + QString::number( dstH ) + QStringLiteral( ", W:" ) + QString::number( dstW ), DetectionErrorCategory::Preprocess, DetectionErrorCode::PreprocessFailed );
		return std::nullopt;
	}
	// 计算newH、newW等（宽高走FrameHandle接口，与具体句柄类型无关）
	const int w = frame->width(), h = frame->height();
	if (h <= 0 || w <= 0) {
		m_lastError = DetectionError( QStringLiteral( "[CudaPreprocessor]: GPU帧尺寸无效: H:" ) + QString::number( h ) + QStringLiteral( ", W:" ) + QString::number( w ), DetectionErrorCategory::Preprocess, DetectionErrorCode::InvalidImage );
		return std::nullopt;
	}
	float scale = std::min( dstW * 1.0f / w, dstH * 1.0f / h );
	int newH = static_cast<int>(std::round( h * scale )), newW = static_cast<int>(std::round( w * scale ));
	newH = std::max( 1, std::min( newH, dstH ) );
	newW = std::max( 1, std::min( newW, dstW ) );
	// 进行填充
	float pad_x = (dstW - newW) / 2.0f;
	float pad_y = (dstH - newH) / 2.0f;
	int ix = static_cast<int>(std::round( pad_x ));
	int iy = static_cast<int>(std::round( pad_y ));
	// 分配输出blob显存（float32 NCHW 1×3×dstH×dstW）
	const size_t blobSize = static_cast<size_t>(3) * dstW * dstH * sizeof( float );
	if (!ensureOutputBuffer( blobSize ))
		return std::nullopt; // ensureOutputBuffer已设置m_lastError
	// 填写params:
	CudaLetterboxParams params;
	params.srcW = w;
	params.srcH = h;
	params.dstW = dstW;
	params.dstH = dstH;
	params.newW = newW;
	params.newH = newH;
	params.padX = ix;
	params.padY = iy;
	params.scale = scale;
	params.dst = m_inputBuffer;
	// 按像素格式分发到对应kernel（平面设备指针所有权归帧句柄，kernel只读借用）
	CudaKernelError kernelError;
	bool launchOk = false;
	if (isNv12) {
		// NV12：Y 平面 + 交错 UV 平面
		launchOk = launchNv12LetterboxKernel( plane0.data, plane0.stride, plane1.data, plane1.stride, params, device.stream(), &kernelError );
	}
	else {
		// BGR24 packed：单一平面
		launchOk = launchBgr24LetterboxKernel( plane0.data, plane0.stride, params, device.stream(), &kernelError );
	}
	if (!launchOk) {
		m_lastError = DetectionError(
			QString::fromLocal8Bit( kernelError.hasError ? kernelError.message : "kernel launch 失败" ),
			DetectionErrorCategory::Preprocess,
			DetectionErrorCode::PreprocessFailed );
		return std::nullopt;
	}
	// 回填letterbox信息
	GpuPreprocessResult result;
	result.gpuData = m_inputBuffer;
	result.letterboxInfo.orig_w = w;
	result.letterboxInfo.orig_h = h;
	result.letterboxInfo.new_w = newW;
	result.letterboxInfo.new_h = newH;
	result.letterboxInfo.pad_w = ix;
	result.letterboxInfo.pad_h = iy;
	result.letterboxInfo.scale = scale;
	return std::make_optional( result );
}
