#pragma once
#include <QUrl>
#include <QVector>
#include "FrameSource.h"
#include "DecodeTypes.h"
#include <FfmpegDecoder.h>
class FfmpegFrameSource : public FrameSource {
public:
	// 创建本地文件帧源（视频文件）
	static FrameSourcePtr forLocalFile(QStringView filePath, MemoryType memoryType = MemoryType::CPU);
	// 创建设备帧源（摄像头/相机），使用 ffmpeg 采集（dshow/avfoundation/v4l2）
	// formatName 为空时使用当前平台的默认输入格式；设备无效返回空指针
	static FrameSourcePtr forDevice( const FfmpegDeviceInfo& device, const QString& formatName = QString(), MemoryType memoryType = MemoryType::CPU );
	// 按设备友好名（或原始标识）创建帧源；名字大小写/首尾空白/部分匹配均可
	static FrameSourcePtr forDevice( const QString& nameOrId, const QString& formatName = QString(), MemoryType memoryType = MemoryType::CPU );
	// 枚举指定输入格式下的 video 类型输入设备（阻塞操作）
	static FfmpegDeviceInfoList enumerateVideoDevices( const QString& formatName = QString() );
	// 当前平台的默认视频采集输入格式（Windows->"dshow"）
	static QString defaultVideoInputFormat();
	~FfmpegFrameSource()override;
	bool open() override;
	bool isOpened()const override;
	void close()override;
	FrameSourceType type()const override;
	FrameHandlePtr getFrame()override;
	size_t totalFrames()const override;
	double fps()const override;
	cv::Size frameSize()const override;
	QString displayName()const override;
	QString lastError()const override;
	MemoryType memoryType()const override { return m_memoryType; }	// 帧句柄的内存类型,CPU/GPU
	// 是否总帧数是估算的（true表示 totalFrames 由时长×帧率估算，可能不准确）
	bool isTotalFramesEstimated()const;

private:
	FfmpegFrameSource(const DecodeConfig& config, MemoryType memoryType = MemoryType::CPU );
	FfmpegFrameSource() = default;
	FfmpegDecoder m_decoder;
	DecodeConfig m_config; // 解码配置，使用抽象工厂函数创建对应的配置
	MemoryType m_memoryType = MemoryType::CPU;	// 帧句柄的内存类型,CPU/GPU
	QString m_error;
	QUrl m_url;

};