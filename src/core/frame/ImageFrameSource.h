#pragma once
#include "FrameSource.h"

// 单张图片帧源
// 固定为CPU内存
class ImageFrameSource : public FrameSource {
public:
	// 创建单图帧源（只用CPU的OpenCV的imread，固定产出CPU帧）
	static FrameSourcePtr forFile(QStringView path);

	~ImageFrameSource() override;

	bool open() override;	// 读取图片，缓存尺寸
	bool isOpened() const override { return m_opened; }
	void close() override;

	FrameSourceType type() const override { return FrameSourceType::SingleImage; }	// 单图
	FrameHandlePtr getFrame() override;	// 仅一帧，取出后返回空指针
	size_t totalFrames() const override { return 1; }	// 固定 1
	double fps() const override { return 0.0; }	// 固定 0
	cv::Size frameSize() const override { return m_size; }
	QString displayName() const override { return m_filePath; }
	QString lastError() const override { return m_error; }
	MemoryType memoryType() const override { return MemoryType::CPU; }	// 固定 CPU

private:
	explicit ImageFrameSource(QStringView filePath);

	cv::Mat m_image;			// open() 时读入并缓存
	QString m_filePath;
	QString m_error;
	cv::Size m_size;
	bool m_imageTaken = false;	// 单图只有一帧
	bool m_opened = false;
};
