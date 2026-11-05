#include "CpuPreprocessor.h"
#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>
#include <algorithm>
#include <cmath>
CpuPreprocessResult CpuPreprocessor::letterbox( const FrameHandlePtr& handle, int dst_w, int dst_h, cv::Scalar color, int interp ) {
	// 复位
	m_lastError = DetectionError();
	CpuPreprocessResult result;
	const auto& cpuHandle = std::dynamic_pointer_cast<CpuFrameHandle>(handle);
	if (!cpuHandle) {
		m_lastError = DetectionError( QStringLiteral( "[CpuPreprocessor]: 转换帧句柄失败，仅支持 CpuFrameHandle 进行预处理" ),
			DetectionErrorCategory::Preprocess, DetectionErrorCode::PreprocessFailed );
		return result;
	}
	// 检查图像是否合法
	if (cpuHandle->constMat().empty() || cpuHandle->constMat().channels() != 3) {
		m_lastError = DetectionError(
			QStringLiteral( "[CpuPreprocessor]: 输入图像无效: H:" ) + QString::number( cpuHandle->height() ) + QStringLiteral( ", W:" ) + QString::number( cpuHandle->width() ) + QStringLiteral( ", channels: " ) + QString::number( cpuHandle->channels() ),
			DetectionErrorCategory::Image,
			DetectionErrorCode::InvalidImage );
		return result;
	}
	if (dst_h <= 0 || dst_w <= 0) {
		m_lastError = DetectionError(
			QStringLiteral( "[CpuPreprocessor]: 目标尺寸非法: H:" ) + QString::number( dst_h ) + QStringLiteral( ", W:" ) + QString::number( dst_w ),
			DetectionErrorCategory::Preprocess,
			DetectionErrorCode::PreprocessFailed );
		return result;
	}
	// 获取缩放比例(以最小为准,防止越界)以及新图的尺寸
	const auto& src = cpuHandle->constMat();
	int src_w = src.cols, src_h = src.rows;
	float scale = std::min( static_cast<float>(dst_w) / src_w, static_cast<float>(dst_h) / src_h );
	int new_w = static_cast<int>(std::round( src_w * scale ));
	int new_h = static_cast<int>(std::round( src_h * scale ));
	new_w = std::max( 1, std::min( new_w, dst_w ) );
	new_h = std::max( 1, std::min( new_h, dst_h ) );
	// 缩放到新尺寸
	cv::resize( src, result.image, cv::Size( new_w, new_h ), 0, 0, interp );
	// 进行填充
	float pad_x = (dst_w - new_w) / 2.0f;
	float pad_y = (dst_h - new_h) / 2.0f;
	cv::Mat canvas( dst_h, dst_w, result.image.type(), color );	  // 画布填充
	int ix = static_cast<int>(std::round( pad_x ));
	int iy = static_cast<int>(std::round( pad_y ));
	// 将缩放后的图像拷贝到画布上（画布多余的部分作为填充）
	result.image.copyTo( canvas( cv::Rect( ix, iy, new_w, new_h ) ) );

	result.image = canvas;
	// 回填信息
	result.letterboxInfo.new_w = new_w;
	result.letterboxInfo.new_h = new_h;
	result.letterboxInfo.orig_w = src_w;
	result.letterboxInfo.orig_h = src_h;
	result.letterboxInfo.pad_w = static_cast<int>(std::round( pad_x ));
	result.letterboxInfo.pad_h = static_cast<int>(std::round( pad_y ));
	result.letterboxInfo.scale = scale;

	return result;
}

bool CpuPreprocessor::col_nor_nchw( const cv::Mat& src, cv::Mat& chw, int dst_w, int dst_h ) {
	// 复位
	m_lastError = DetectionError();
	if (src.empty() || src.channels() != 3) {
		m_lastError = DetectionError(
			QStringLiteral( "[CpuPreprocessor]: 输入图像无效: H:" ) + QString::number( src.rows ) + QStringLiteral( ", W:" ) + QString::number( src.cols ) + QStringLiteral( ", channels: " ) + QString::number( src.channels() ),
			DetectionErrorCategory::Image,
			DetectionErrorCode::InvalidImage );
		return false;
	}
	try {
		cv::dnn::blobFromImage( src, chw, 1.0 / 255.0, cv::Size( dst_w, dst_h ), cv::Scalar(), true, false, CV_32F );
	}
	catch (cv::Exception& e) {
		m_lastError = DetectionError(
			QStringLiteral( "[CpuPreprocessor]: 转换为颜色格式、NCHW时发生OpenCV 异常: " ) + QString::fromStdString( e.what() ),
			DetectionErrorCategory::Preprocess,
			DetectionErrorCode::PreprocessFailed );
		return false;
	}
	return true;
}

std::optional<CpuPreprocessResult> CpuPreprocessor::preprocess( const FrameHandlePtr& handle, int dst_w, int dst_h, cv::Scalar color, int interp ) {
	m_lastError = DetectionError(); // 复位
	// 先进行 letterbox 预处理
	CpuPreprocessResult result = letterbox( handle, dst_w, dst_h, color, interp );
	if (!result.isValid()) {
		return std::nullopt; // letterbox已经设置了 m_lastError
	}
	// 再进行颜色转换、归一化、NCHW布局转换
	cv::Mat blob ; 
	if (!col_nor_nchw( result.image, blob, dst_w, dst_h )) {
		return std::nullopt; // col_nor_nchw已经设置了 m_lastError
	}
	result.image = blob; // 替换为NCHW格式的图像
	return std::make_optional( result );
}

const DetectionError& CpuPreprocessor::lastError() const {
	return m_lastError;
}
