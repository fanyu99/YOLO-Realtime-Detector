#include "FileFrameSource.h"

#include <opencv2/imgcodecs.hpp>

#include "Logger.h"

namespace {
	// 摄像头：让驱动只缓 1 帧,只缓存最新帧
	// 推理慢于出帧时，cap.read() 会返回缓存的旧帧
	void limitCameraBuffer(cv::VideoCapture& cap) {
		if (cap.set(cv::CAP_PROP_BUFFERSIZE, 1))
			Logger::instance()->info(QStringLiteral("[FileFrameSource]: 摄像头缓冲已设为 1 帧"));
		else
			Logger::instance()->warning(QStringLiteral(
				"[FileFrameSource]: 当前后端不支持设置缓冲帧数，实时预览可能逐渐滞后"));
	}
}

// 文件帧源: 单图/视频文件,路径
FrameSourcePtr FileFrameSource::forFile(QStringView path) {
	const FrameSourceType type = isVideoPath(path) ? FrameSourceType::VideoFile
												   : FrameSourceType::SingleImage;
	return FrameSourcePtr(new FileFrameSource(path, type, -1));	 // 返回对应的路径下的源对象
}

// 摄像头帧源,CameraId
FrameSourcePtr FileFrameSource::forCamera(int cameraId) {
	if (cameraId < 0)
		return nullptr;
	return FrameSourcePtr(new FileFrameSource(QString(), FrameSourceType::Camera, cameraId));
}

FileFrameSource::FileFrameSource(QStringView filePath, FrameSourceType type, int cameraId)
	: m_filePath(filePath.toString()), m_type(type), m_cameraId(cameraId) {
}

FileFrameSource::~FileFrameSource() {
	close();
}

// 打开帧源,根据类型读取文件/打开摄像头,然后缓存FPS/帧数/尺寸
bool FileFrameSource::open() {
	if (m_opened)
		return true;

	m_error.clear();
	m_imageTaken = false;

	switch (m_type) {
		// 单图,imread
	case FrameSourceType::SingleImage:
		m_image = cv::imread(m_filePath.toStdString());
		if (m_image.empty()) {
			m_error = QStringLiteral("图片加载失败: ") + m_filePath;
			return false;
		}
		m_totalFrames = 1;
		m_size = m_image.size();
		break;
	// 视频流/摄像头
	case FrameSourceType::VideoFile:
	case FrameSourceType::Camera: {
		const bool ok = (m_type == FrameSourceType::Camera)
			? m_cap.open(m_cameraId)
			: m_cap.open(m_filePath.toStdString());
		if (!ok || !m_cap.isOpened()) {
			m_error = QStringLiteral("无法打开视频源!");
			return false;
		}
		// 摄像头仅缓存最新帧,防止延迟过高
		if (m_type == FrameSourceType::Camera)
			limitCameraBuffer(m_cap);

		// 摄像头总帧数设为0 （总量未知,UI 显示忙碌）
		const double count = m_cap.get(cv::CAP_PROP_FRAME_COUNT);
		m_totalFrames = (m_type == FrameSourceType::Camera || count <= 0.0)
			? 0 : static_cast<size_t>(count);
		m_fps = m_cap.get(cv::CAP_PROP_FPS);
		m_size = cv::Size(static_cast<int>(m_cap.get(cv::CAP_PROP_FRAME_WIDTH)),
						  static_cast<int>(m_cap.get(cv::CAP_PROP_FRAME_HEIGHT)));
		break;
	}
	}

	m_opened = true;
	return true;
}

void FileFrameSource::close() {
	if (m_cap.isOpened())
		m_cap.release();
	m_opened = false;
}

cv::Mat FileFrameSource::getFrame() {
	if (!m_opened)
		return {};

	if (m_type == FrameSourceType::SingleImage) {
		if (m_imageTaken)			// 单图只产一帧，否则调用方的 while 循环不会结束
			return {};
		m_imageTaken = true;
		return m_image;
	}

	cv::Mat frame;
	m_cap.read(frame);
	return frame;					
}
