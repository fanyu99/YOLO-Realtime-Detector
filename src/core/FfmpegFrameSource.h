#pragma once
#include <QUrl>
#include "FrameSource.h"
#include <FfmpegDecoder.h>
class FfmpegFrameSource : public FrameSource {
public:
	// 创建本地文件帧源（视频文件）
	static FrameSourcePtr forLocalFile( QStringView filePath );
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
	// 是否总帧数是估算的（true表示 totalFrames 由时长×帧率估算，可能不准确）
	bool isTotalFramesEstimated()const;

private:
	FfmpegFrameSource(const DecodeConfig& config );
	FfmpegFrameSource() = default;
	FfmpegDecoder m_decoder;
	DecodeConfig m_config; // 解码配置，使用抽象工厂函数创建对应的配置
	QString m_error;
	QUrl m_url;

};