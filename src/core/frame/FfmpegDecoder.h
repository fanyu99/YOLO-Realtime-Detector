#pragma once
#include <QString>
#include <QUrl>
#include <QElapsedTimer>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
extern "C" {
#include <libavdevice/avdevice.h>
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavformat/avformat.h>
#include <libavutil/time.h>
#include <libavutil/opt.h>
#include <libavutil/error.h>
#include <libavutil/pixdesc.h>
#include <libavutil/avconfig.h>
#include <libswscale/swscale.h>
}
#include "DecodeTypes.h"
namespace FfmpegDecode {
	// 将ffmpeg错误转为QString
	inline QString ffmepegError( int errorCode ) {
		char errorBuffer[AV_ERROR_MAX_STRING_SIZE]{};
		av_strerror( errorCode, errorBuffer, sizeof( errorBuffer ) );
		return QString::fromUtf8( errorBuffer );
	}
	// ====== 智能指针保护ffmpeg资源========
	// 在离开作用域时自动释放资源
	// 对于存在引用计数的资源，例如 AVFrame 和 AVPacket，注意需要自行调用相关的unref
	struct AVFormatContextDeleter {
		void operator()( AVFormatContext* ctx ) const noexcept {
			if (ctx)
				avformat_close_input( &ctx );
		}
	};
	struct AVCodecContextDeleter {
		void operator()( AVCodecContext* ctx ) const noexcept {
			if (ctx)
				avcodec_free_context( &ctx );
		}
	};
	struct AVFrameDeleter {
		void operator()( AVFrame* frame ) const noexcept {
			if (frame)
				av_frame_free( &frame );
		}
	};
	struct AVPacketDeleter {
		void operator()( AVPacket* packet ) const noexcept {
			if (packet)
				av_packet_free( &packet );
		}
	};
	// 方便后续使用
	using AVFormatContextPtr = std::unique_ptr<AVFormatContext, AVFormatContextDeleter>;
	using AVFramePtr = std::unique_ptr<AVFrame, AVFrameDeleter>;
	using AVPacketPtr = std::unique_ptr<AVPacket, AVPacketDeleter>;
	using AVCodecContextPtr = std::unique_ptr<AVCodecContext, AVCodecContextDeleter>;
	// ======================================
	// 读取超时、连接超时

	// 中断上下文
	struct InterruptContext {
		int timeoutMs{ DEFAULT_DECODE_READ_TIMEOUT_MS }; // 超时时间
		// 原子变量用于中断回调函数
		std::atomic<bool> pause{ false }; // 外部的中断标志
		std::atomic<bool>stop{ false }; // 外部的停止标志，用于解码会话的停止（直接退出循环）
		QElapsedTimer timer; // 计时器

		explicit InterruptContext( int timeout = DEFAULT_DECODE_READ_TIMEOUT_MS )
			: timeoutMs( timeout ) {
			timer.start();  // 开始计时
		}

		// 每次开始一个新的阻塞操作（open / 单次 read）前调用，
		// 让超时以“本次操作”为基准，而不是整个连接期间累计。
		void restart() {
			timer.start();
		}

		// 重置停止和暂停标志
		void resetStatus() {
			pause.store( false, std::memory_order_relaxed );
			stop.store( false, std::memory_order_relaxed );
		}

		// 是否超时
		bool isTimeout() const {
			return timer.isValid() && timer.elapsed() > timeoutMs;
		}
	};

	// 中断回调函数，用于在读取RTSP流时检查是否超时或中断
	// ffmpeg内部会进行周期性的调用回调函数，0就继续，1就中断
	inline int interrupt_cb( void* opaque ) {
		auto* ctx = static_cast<InterruptContext*>(opaque);
		if (!ctx)
			return 0;
		// 检查是否终止/停止
		if (ctx->pause.load( std::memory_order_relaxed ) || ctx->stop.load( std::memory_order_relaxed ))
			return 1;
		// 检查是否超时
		if (ctx->isTimeout())
			return 1;
		return 0;
	}


};

/*
FfmpegDecoder
	│
	├── 管理 FFmpeg 资源
	├── open()
	├── readNextFrame()
	├── close()
	└── 提供流信息和错误信息
*/
// 单独的通用的ffmpeg解码器，进行解码
class FfmpegDecoder {
public:
	using AVPacketPtr = FfmpegDecode::AVPacketPtr;
	using AVFramePtr = FfmpegDecode::AVFramePtr;
	using AVCodecContextPtr = FfmpegDecode::AVCodecContextPtr;
	using AVFormatContextPtr = FfmpegDecode::AVFormatContextPtr;
	using frameCallback = std::function<void( const AVFrame*, quint64 readTimeUs, quint64 decodedTimeUs )>;
	explicit FfmpegDecoder() = default;
	explicit FfmpegDecoder( const DecodeConfig& config );
	~FfmpegDecoder();

	QUrl getUrl() const;
	StreamInfo getStreamInfo() const;
	// 打开
	[[nodiscard]] bool open();
	// 是否打开
	[[nodiscard]] bool isOpened()const;
	// 关闭
	void close();
	// 开启读取循环
	[[nodiscard]] bool startDecodingLoop();
	// 设置配置
	void setConfig( const DecodeConfig& config );
	// 设置回调函数
	void setFrameCallback(const frameCallback& callback);

	// 读取下一帧，返回读取状态
	[[nodiscard]] ReadStatus readNextFrame();
	// 借用指针,在下一次readNextFrame()之前有效(后续读取会unref前面的AVFrame，m_frame后面也会被覆盖)
	[[nodiscard]] const AVFrame* getCurrentFrame()const;

	// 请求停止（线程安全）
	void requestStop();
	// 是否请求停止
	[[nodiscard]] bool isStopRequested()const;
	// 设置暂停/恢复（线程安全）
	void setPaused( bool pause );
	// 是否暂停
	[[nodiscard]] bool isPaused()const;

	// 获取最后一次错误
	[[nodiscard]] DecodeError getLastError()const;
	// 获取统计信息
	[[nodiscard]] DecodeStatistics getStatistics()const;
	// 当前已解码帧数(从 1 开始)，与 getCurrentFrame() 的帧对应
	[[nodiscard]] int decodeIndex()const { return m_decodeIndex; }

	// 清理所有资源
	void releaseResources();
	// 清除所有资源以及重置配置信息
	void reset();
private:
	// 打开输入（avformat_open_input + find_stream_info + 查找视频流）
	[[nodiscard]] bool openInput();
	// 	根据视频流初始化解码器和 packet/frame
	[[nodiscard]] bool initDecoder();
	// 记录当前的流信息
	void recordStreamInfo(  AVStream* stream, const AVCodec* codec );
	// 循环接收解码器解码packet后输出的帧
	[[nodiscard]] bool receiveFrames();
	// 读取packet并解码，阻塞直到停止、EOF或出错
	[[nodiscard]] bool readAndDecodeLoop();
	// 帧回调
	frameCallback m_frameCallback{nullptr};

	StreamInfo m_streamInfo{}; // 流信息
	DecodeConfig m_config{}; // 配置
	DecodeStatistics m_decodeStatistics{}; // 统计信息
	DecodeError m_lastError{}; // 上次错误
	int m_videoStreamIndex{ -1 }; // 视频流索引
	int m_decodeIndex{ 0 }; // 解码帧索引
	bool has_pendingPacket{ false }; // 是否有未发送的packet
	bool flushEof{ false }; // 是否已经flush过EOF

	std::mutex m_pauseMutex;  // 用来暂停的互斥锁，中断时阻塞解码
	std::condition_variable m_pauseCV; // 用来暂停的条件变量，中断时阻塞解码
	FfmpegDecode::InterruptContext m_interruptContex; // 中断上下文
	std::atomic<bool>m_running{ false };   // 是否正在工作（包含暂停）

	AVFormatContextPtr m_formatContext{ nullptr }; // 格式上下文
	AVCodecContextPtr m_codecContext{ nullptr }; // 编解码器上下文
	AVPacketPtr m_packet{ nullptr }; // 压缩包
	AVFramePtr m_frame{ nullptr }; // 解码后的帧
};