#pragma once
#include <opencv2/core.hpp>
#include <QString>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

// 复用的像素格式转换上下文：分辨率或像素格式变化时自动重建
class SwsContextCache {
public:
	SwsContextCache() = default;
	~SwsContextCache() {
		if (m_context)
			sws_freeContext( m_context );
	}
	SwsContextCache( const SwsContextCache& ) = delete;
	SwsContextCache& operator=( const SwsContextCache& ) = delete;

	SwsContext* get( int width, int height, AVPixelFormat srcFormat, AVPixelFormat dstFormat ) {
		m_context = sws_getCachedContext(
			m_context,
			width, height, srcFormat,
			width, height, dstFormat,
			SWS_BILINEAR, nullptr, nullptr, nullptr );
		return m_context;
	}

private:
	SwsContext* m_context{ nullptr };
};

// 解码出的软件帧 AVFrame -> cv::Mat（BGR 三通道）
// 硬解帧（CUDA/D3D11等）没有可直接读取的像素数据，返回空Mat（需先 av_hwframe_transfer_data 下载）
inline cv::Mat avFrameToMat( const AVFrame* frame ) {
	if (!frame || frame->width <= 0 || frame->height <= 0 || !frame->data[0])
		return cv::Mat();

	const auto sourceFormat = static_cast<AVPixelFormat>(frame->format);
	const AVPixFmtDescriptor* descriptor = av_pix_fmt_desc_get( sourceFormat );
	if (!descriptor || (descriptor->flags & AV_PIX_FMT_FLAG_HWACCEL))
		return cv::Mat();

	thread_local SwsContextCache swsCache;
	SwsContext* sws = swsCache.get( frame->width, frame->height, sourceFormat, AV_PIX_FMT_BGR24 );
	if (!sws)
		return cv::Mat();

	cv::Mat mat( frame->height, frame->width, CV_8UC3 );
	if (mat.empty())
		return mat;

	uint8_t* destinationData[4]{ mat.data, nullptr, nullptr, nullptr };
	const int destinationLinesize[4]{ static_cast<int>(mat.step), 0, 0, 0 };
	const int scaled = sws_scale(
		sws,
		frame->data, frame->linesize, 0, frame->height,
		destinationData, destinationLinesize );
	if (scaled <= 0)
		return cv::Mat();

	return mat;
}
