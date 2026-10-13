#pragma once
#include "FrameSource.h"

#include <opencv2/videoio.hpp>

// 以「文件路径」或「摄像头设备号」为输入的帧源。
// 是图片/视频/摄像头由工厂决定(看后缀 / 看是否给了设备号), 之后一律通过
class FileFrameSource : public FrameSource {
public:
	// 按后缀判定类型: 视频后缀 -> VideoFile, 其余一律按 SingleImage 处理
	// (认不出的路径由 open() 以"图片加载失败"报错, 与批量作业的既有行为一致)
	static FrameSourcePtr forFile(QStringView path);
	static FrameSourcePtr forCamera(int cameraId);	// cameraId < 0 返回 nullptr

	~FileFrameSource() override;

	bool open() override;	  // 读取文件/打开视频流/摄像头,缓存fps,尺寸和总帧数
	bool isOpened() const override { return m_opened; }
	void close() override;

	FrameSourceType type() const override { return m_type; }
	cv::Mat getFrame() override;
	size_t totalFrames() const override { return m_totalFrames; }
	double fps() const override { return m_fps; }
	cv::Size frameSize() const override { return m_size; }
	QString displayName() const override { return m_filePath; }	// 摄像头无源文件,返回空串
	QString lastError() const override { return m_error; }

private:
	FileFrameSource(QStringView filePath, FrameSourceType type, int cameraId);

	QString m_filePath;
	FrameSourceType m_type;
	int m_cameraId = -1;

	cv::VideoCapture m_cap;		// VideoFile / Camera 用
	cv::Mat m_image;			// SingleImage 用
	bool m_imageTaken = false;	// 单图只有一帧

	bool m_opened = false;
	size_t m_totalFrames = 0;
	double m_fps = 0.0;
	cv::Size m_size;
	QString m_error;
};
