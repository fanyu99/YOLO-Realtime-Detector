#include "ImageFrameSource.h"
#include "CpuFrameHandle.h"

#include <opencv2/imgcodecs.hpp>


// 创建单图帧源
FrameSourcePtr ImageFrameSource::forFile( QStringView path ) {
	return FrameSourcePtr( new ImageFrameSource( path ) );
}

ImageFrameSource::ImageFrameSource( QStringView filePath )
	: m_filePath( filePath.toString() ) {
}

ImageFrameSource::~ImageFrameSource() {
	close();
}

// 打开帧源：读取图片并缓存
bool ImageFrameSource::open() {
	if (m_opened)
		return true;

	m_error.clear();
	m_imageTaken = false;

	m_image = cv::imread( m_filePath.toStdString() );
	if (m_image.empty()) {
		m_error = QStringLiteral( "图片加载失败: " ) + m_filePath;
		return false;
	}
	m_size = m_image.size();

	m_opened = true;
	return true;
}

void ImageFrameSource::close() {
	m_opened = false;
}

FrameHandlePtr ImageFrameSource::getFrame() {
	if (!m_opened)
		return nullptr;
	if (m_imageTaken)	// 单图只有一帧，取过即返回空
		return nullptr;
	m_imageTaken = true;
	return std::make_shared<CpuFrameHandle>( m_image, 0 );
}
