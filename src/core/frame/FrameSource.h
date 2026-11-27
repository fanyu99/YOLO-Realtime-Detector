// 使用策略模式
// 帧来源:
// 目前支持两种:
// 1. 使用摄像头/视频文件/图片文件作为帧来源
// 2. 使用相机类获取帧源
#pragma once
#include <opencv2/core.hpp>
#include <QString>
#include <QStringList>
#include <memory>
#include <optional>
#include "FrameHandle.h"
// 相机触发模式（工业相机）
enum class CameraTriggerMode {
	Off,      // 关闭触发,连续采集
	Software, // 软件触发
	Line0,    // 硬触发,使用Line0
	Line1,    // 硬触发,使用Line1
	Line2,    // 硬触发,使用Line2
};
// 帧源类型
enum class FrameSourceType {
	SingleImage,	// 单张照片
	VideoFile,							 // 视频文件
	Camera					// 摄像头/相机
};
// 帧源设备信息
// serial 为设备唯一标识
struct FrameSourceDeviceInfo {
	QString serial;
	QString model;
	QString userDefinedName;
	bool online = false;
	CameraTriggerMode triggerMode = CameraTriggerMode::Off;
};
// 帧源描述,类型+ 路径/设备名
struct FrameSourceDescription {
	std::optional<QString>filePath;	    // 文件路径（图片/视频）
	std::optional<QString>nameOrId;	  // 设备名（摄像头）
	std::optional<QString>formatName;  // 摄像头输入格式（dshow/avfoundation/v4l2）
	std::optional<QString>serial; // 工业相机/插件设备的唯一标识
	CameraTriggerMode triggerMode = CameraTriggerMode::Off;	// 工业相机触发模式
	std::optional<QString> pluginId;	// 非空表示由该帧源插件创建
	FrameSourceType type;
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
	virtual FrameHandlePtr getFrame() = 0;	   // 获取帧,出现错误返回空指针
	virtual size_t totalFrames() const { return 0; } // 返回总帧,未知返回0,例如摄像头返回0
    virtual double fps()const { return 0.0; }; // 返回帧数,图片为0,resultSaver根据为0不设置VideoWriter
	virtual cv::Size frameSize()const { return cv::Size(); } // 返回帧大小
	virtual QString displayName() const = 0; // 路径/摄像头编号/相机名称等
	virtual QString lastError()const { return {}; } // 获取错误
	virtual MemoryType memoryType()const = 0;	// 帧句柄的内存类型,CPU/GPU
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
inline bool isImagePath(QStringView path) {
	static const QStringList kImageExts = {
		QStringLiteral(".jpg"), QStringLiteral(".jpeg"), QStringLiteral(".png"),
		QStringLiteral(".bmp"), QStringLiteral(".webp")
	};
	const QString lower = path.toString().toLower();
	for (QStringView ext : kImageExts)
		if (lower.endsWith(ext))
			return true;
	return false;
}

