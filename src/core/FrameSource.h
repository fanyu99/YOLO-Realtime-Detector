// 使用策略模式
// 帧来源:
// 目前支持两种:
// 1. 使用摄像头/视频文件/图片文件作为帧来源
// 2. 使用相机类获取帧源
// 目前仅支持返回cv::Mat的帧源
#pragma once
#include <opencv2/core.hpp>
#include <QString>
#include <QStringList>
#include <memory>
enum class FrameSourceType {
	SingleImage,	// 单张照片
	VideoFile,							 // 视频文件
	Camera					// 摄像头/相机
};
// 抽象类
class FrameSource {
public:
	FrameSource() = default;
	virtual ~FrameSource() = default;

	// 在Worker线程进行调用
	virtual bool open() = 0;
	virtual bool isOpened() const = 0;
	virtual void close() = 0;

	virtual FrameSourceType type() const = 0;	// 帧源种类: worker据此选择取帧方式, 上层据此派生JobType
	virtual cv::Mat getFrame() = 0;	   // 获取帧,出现错误返回空Mat
	virtual size_t totalFrames() const { return 0; } // 返回总帧,未知返回0,例如摄像头返回0
    virtual double fps()const { return 0.0; }; // 返回帧数,图片为0,resultSaver根据为0不设置VideoWriter
	virtual cv::Size frameSize()const { return cv::Size(); } // 返回帧大小
	virtual QString displayName() const = 0; // 路径/摄像头编号/相机名称等
	virtual QString lastError()const { return {}; } // 获取错误
};
using FrameSourcePtr = std::shared_ptr<FrameSource>;		// 使用抽象工厂


// 判断是否为视频路径。
inline bool isVideoPath(QStringView path) {
	static const QStringList kVideoExts = {
		QStringLiteral(".mp4"), QStringLiteral(".avi"), QStringLiteral(".mov"),
		QStringLiteral(".mkv"), QStringLiteral(".wmv")
	};
	const QString lower = path.toString().toLower();
	for (QStringView ext : kVideoExts)
		if (lower.endsWith(ext))
			return true;
	return false;
}

// 判断是否为图片路径
inline bool isImagePath(const QString& path) {
	static const QStringList kImageExts = {
		QStringLiteral(".jpg"), QStringLiteral(".jpeg"), QStringLiteral(".png"),
		QStringLiteral(".bmp"), QStringLiteral(".webp")
	};
	const QString lower = path.toLower();
	for (const QString& ext : kImageExts)
		if (lower.endsWith(ext))
			return true;
	return false;
}

