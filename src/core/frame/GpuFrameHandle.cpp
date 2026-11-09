#include "GpuFrameHandle.h"

namespace {
	// 各像素格式的plane数量，未知/不支持的格式返回0
	int planeCountOf( AVPixelFormat format ) {
		switch (format) {
		case AV_PIX_FMT_NV12:
		case AV_PIX_FMT_CUDA: // NVDEC输出，显存布局为NV12：Y + 交错UV
			return 2;
		case AV_PIX_FMT_YUV420P:
			return 3;
		case AV_PIX_FMT_BGR24:
		case AV_PIX_FMT_BGRA:
		case AV_PIX_FMT_RGB24:
		case AV_PIX_FMT_GRAY8:
			return 1;
		default:
			return 0;
		}
	}
}

GpuFrameHandle::GpuFrameHandle( AVFrame* frame ) {
	if (frame)
		m_frame.reset( av_frame_clone( frame ) ); // 引用计数+1，共享底层buffer
}

GpuFrameHandle::~GpuFrameHandle() {
}

void GpuFrameHandle::setFrameIndex( int index ) {
	if (index >= 0)
		m_frameIndex = index;
}

int GpuFrameHandle::height() const {
	return m_frame ? m_frame->height : 0;
}

int GpuFrameHandle::width() const {
	return m_frame ? m_frame->width : 0;
}

int GpuFrameHandle::channels() const {
	if (!m_frame)
		return 0;
	switch (m_frame->format) {
	case AV_PIX_FMT_BGR24:
	case AV_PIX_FMT_RGB24:
		return 3;
	case AV_PIX_FMT_BGRA:
		return 4;
	case AV_PIX_FMT_GRAY8:
		return 1;
	default:
		return 0; // planar(NV12/YUV420P)与硬件帧不是交错布局，0表示不能按通道访问
	}
}

int GpuFrameHandle::stride() const {
	if (!m_frame)
		return 0;
	return m_frame->linesize[0];
}

int GpuFrameHandle::frameIndex() const {
	return m_frameIndex;
}

PixelFormat GpuFrameHandle::pixelFormat() const {
	if (!m_frame)
		return PixelFormat::Other;
	switch (m_frame->format) {
	case AV_PIX_FMT_BGR24:
		return PixelFormat::BGR24;
	case AV_PIX_FMT_BGRA:
		return PixelFormat::BGRA;
	case AV_PIX_FMT_RGB24:
		return PixelFormat::RGB24;
	case AV_PIX_FMT_GRAY8:
		return PixelFormat::GRAY;
	case AV_PIX_FMT_NV12:
	case AV_PIX_FMT_CUDA: 
		return PixelFormat::NV12;
	case AV_PIX_FMT_YUV420P:
		return PixelFormat::YUV420P;
	default:
		return PixelFormat::Other;
	}
}

PlaneDescriptor GpuFrameHandle::plane( int planeIndex ) const {
	PlaneDescriptor desc{};
	if (!m_frame)
		return desc;
	const int planeCount = planeCountOf( static_cast<AVPixelFormat>(m_frame->format) );
	if (planeIndex < 0 || planeIndex >= planeCount)
		return desc;
	desc.data = m_frame->data[planeIndex]; // 硬件帧时为显存设备指针
	desc.stride = m_frame->linesize[planeIndex];
	desc.width = m_frame->width;
	desc.height = m_frame->height;
	switch (m_frame->format) {
	case AV_PIX_FMT_NV12:
	case AV_PIX_FMT_CUDA:
		if (planeIndex == 1) { // UV平面半宽×半高
			desc.width = (m_frame->width + 1) / 2;
			desc.height = m_frame->height / 2;
		}
		break;
	case AV_PIX_FMT_YUV420P:
		if (planeIndex >= 1) { // U/V平面半宽×半高
			desc.width = (m_frame->width + 1) / 2;
			desc.height = (m_frame->height + 1) / 2;
		}
		break;
	default:
		break;
	}
	return desc;
}

const AVFrame* GpuFrameHandle::constFrame() const {
	return m_frame.get();
}

bool GpuFrameHandle::isValid() const {
	return m_frame != nullptr;
}

MemoryType GpuFrameHandle::memoryType() const {
	return MemoryType::GPU;
}
