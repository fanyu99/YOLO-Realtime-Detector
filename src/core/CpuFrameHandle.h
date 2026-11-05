#pragma once
#include <opencv2/core.hpp>
#include "FrameHandle.h"
// CPU帧句柄：内部存储cv::Mat,Mat自动管理生命周期
class CpuFrameHandle final : public FrameHandle{
public:
	explicit CpuFrameHandle( const cv::Mat& mat, int index = -1 );
	CpuFrameHandle() = default;
	~CpuFrameHandle()override;
	void setFrameIndex( int index ) override;
	int width() const override;
	int height() const override;
	int channels() const override;
	int stride() const override;
	int frameIndex() const override;
	PixelFormat pixelFormat() const override;
	PlaneDescriptor plane( int planeIndex ) const override;
	// 获取内部数据副本Mat
	cv::Mat cloneMat()const;
	// 获取只读数据
	const cv::Mat& constMat()const;
	// 获取内部数据引用
	cv::Mat& referenceMat();
	// 获取大小
	cv::Size size()const;
private:
	cv::Mat m_frame; // 内部存储的帧数据
	int m_frameIndex = -1; // 帧序号
};

// 从抽象帧句柄取出 CPU 帧(共享数据，零拷贝)；非 CPU 句柄返回空 Mat
inline cv::Mat toCvMat( const FrameHandlePtr& handle ) {
	if (const auto cpu = std::dynamic_pointer_cast<CpuFrameHandle>( handle ))
		return cpu->constMat();
	return {};
}