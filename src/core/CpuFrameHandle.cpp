#include "CpuFrameHandle.h"
void CpuFrameHandle::setFrameIndex( int index ) {
	m_frameIndex = index;
}
int CpuFrameHandle::width() const {
	return m_frame.cols;
}
int CpuFrameHandle::height() const {
	return m_frame.rows;
}
int CpuFrameHandle::channels() const {
	return m_frame.channels();
}
int CpuFrameHandle::stride() const {
	return m_frame.empty() ? 0 : static_cast<int>(m_frame.step[0]);
}
int CpuFrameHandle::frameIndex() const {
	return m_frameIndex;
}
PixelFormat CpuFrameHandle::pixelFormat() const {
	if (m_frame.empty() || m_frame.depth() != CV_8U) {
		return PixelFormat::Other;
	}
	switch (m_frame.channels()) {
	case 1: return PixelFormat::GRAY;
	case 3: return PixelFormat::BGR24; 
	case 4: return PixelFormat::BGRA;
	default: return PixelFormat::Other;
	}
}
PlaneDescriptor CpuFrameHandle::plane( int planeIndex ) const {
	PlaneDescriptor desc{};
	if (planeIndex == 0) {
		desc = { m_frame.data, static_cast<int>(m_frame.step[0]), m_frame.cols, m_frame.rows };
	}
	return desc;
}
cv::Mat CpuFrameHandle::cloneMat() const {
	return m_frame.clone();
}

cv::Mat& CpuFrameHandle::referenceMat() {
	return m_frame;
}

const cv::Mat& CpuFrameHandle::constMat() const {
	return m_frame;
}

cv::Size CpuFrameHandle::size() const {
	return m_frame.size();
}

CpuFrameHandle::CpuFrameHandle(const cv::Mat& mat, int index )
	: m_frame( mat ), m_frameIndex( index ) {
}
CpuFrameHandle::~CpuFrameHandle() {
}