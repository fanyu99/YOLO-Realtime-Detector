#pragma once
#include <opencv2/core.hpp>
#include "DetectionTypes.h"
#include "CpuFrameHandle.h"
#include <span>
#include <vector>
#include <optional>
#include <QString>

// CPU预处理结果（内部包含预处理后的cv::Mat和letterbox处理后的信息）
struct CpuPreprocessResult {
	cv::Mat image;	// 处理后的图像
	LetterboxInfo letterboxInfo; // letterbox处理信息
	// 判断是否有效：
	bool isValid() const { return !image.empty() && letterboxInfo.isValid(); }
};

// CPU预处理器：letterbox、NCHW布局转换、归一化等操作
// 接收FrameHandlePtr(内部为cv::Mat数据)进行预处理（共享数据）
class CpuPreprocessor {
public:
	// letterbox预处理，返回处理后的处理信息以及创建的新图像,发生错误时返回空的Mat
	 CpuPreprocessResult letterbox( const FrameHandlePtr& handle, int dst_w = 640, int dst_h = 640, cv::Scalar color = cv::Scalar( 114, 114, 114 ), int interp = cv::INTER_LINEAR );
	// 预处理：颜色转换、归一化、NCHW布局	,转换错误返回false(true不保证转换成功)
	 bool col_nor_nchw( const cv::Mat& src, cv::Mat& chw, int dst_w = 640, int dst_h = 640 );
	// 整体的预处理：letterbox + 颜色转换 + NCHW布局,转换错误返回nullopt,成功返回结果
	 std::optional<CpuPreprocessResult> preprocess( const FrameHandlePtr& handle, int dst_w = 640, int dst_h = 640, cv::Scalar color = cv::Scalar( 114, 114, 114 ), int interp = cv::INTER_LINEAR );

	// 获取最近一次错误
	const DetectionError& lastError() const;

private:
	DetectionError m_lastError;	// 最近一次错误

};