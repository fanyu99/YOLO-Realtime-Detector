#include "GpuImageFrameHandle.h"

GpuImageFrameHandle::GpuImageFrameHandle( const cv::Mat& mat, int index )
	: m_frameIndex( index )
	, width_( mat.cols )
	, height_( mat.rows )
	, channels_( mat.channels() )
{
	if (mat.empty())
		return;
	cv::Mat src = mat.isContinuous() ? mat : mat.clone();
	stride_ = static_cast<int>( src.step );
	switch (src.type()) {
	case CV_8UC3:
		pixelFormat_ = PixelFormat::BGR24;
		break;
	// 以下格式不支持上传，但记录具体PixelFormat供上层错误信息debug显示
	case CV_8UC1:
		pixelFormat_ = PixelFormat::GRAY;
		return;
	case CV_8UC4:
		pixelFormat_ = PixelFormat::BGRA;
		return;
	case CV_16UC1:
	case CV_16UC3:
	case CV_32FC1:
	case CV_32FC3:
		pixelFormat_ = PixelFormat::Other;
		return;
	default:	// 其他的格式不支持，直接返回，不分配显存
		pixelFormat_ = PixelFormat::Other;
		return;
	}
	// 将CPU Mat上传到GPU显存
#ifdef YOLO_HAVE_CUDA
	if (!CudaDevice::instance().isValid())
		return;
	void* gpuPtr = nullptr;
	if (cudaMalloc( &gpuPtr, src.total() * src.elemSize() )!= cudaSuccess) {
		cudaGetLastError();
		return;
	}
	if (gpuPtr) {
		if (cudaMemcpy( gpuPtr, src.data, src.total() * src.elemSize(), cudaMemcpyHostToDevice )!= cudaSuccess ) {
			cudaGetLastError();
			cudaFree(gpuPtr);
			gpuPtr = nullptr;
			return;
		}
	}
	m_gpuData.reset(static_cast<uint8_t*>(gpuPtr));
#endif
}

GpuImageFrameHandle::~GpuImageFrameHandle() {
}

int GpuImageFrameHandle::width() const {
	return width_;
}

void GpuImageFrameHandle::setFrameIndex( int index ) {
	m_frameIndex = index;
}

int GpuImageFrameHandle::height() const {
	return height_;
}

int GpuImageFrameHandle::channels() const {
	return channels_;
}
int GpuImageFrameHandle::stride() const {
	return stride_;
}
int GpuImageFrameHandle::frameIndex() const {
	return m_frameIndex;
}

PixelFormat GpuImageFrameHandle::pixelFormat() const {
	return pixelFormat_;
}

bool GpuImageFrameHandle::dataEmpty() const {
	return m_gpuData == nullptr;
}

bool GpuImageFrameHandle::isValid() const {
	return !dataEmpty() && height_ > 0 && width_ > 0 && m_frameIndex >= 0 && pixelFormat_ == PixelFormat::BGR24 && channels_ == 3 && stride_ > 0;
}

PlaneDescriptor GpuImageFrameHandle::plane( int planeIndex ) const {
	PlaneDescriptor desc;
	if (planeIndex != 0 || !isValid())
		return desc;

	desc.data = reinterpret_cast<const uint8_t*>(m_gpuData.get());
	desc.stride = stride_;
	desc.width = width_;
	desc.height = height_;
	return desc;
}

MemoryType GpuImageFrameHandle::memoryType() const {
	return MemoryType::GPU;
}

