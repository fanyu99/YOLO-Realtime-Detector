#pragma once
#include <QString>
#include <QUrl>
#include <QObject>
#include <QElapsedTimer>
#include <atomic>
#include <memory>
#include <mutex>
#include <condition_variable>
#include "RtspTypes.h"
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

// 相关工具
namespace Decode {
	// 将ffmpeg错误转为QString
	inline QString ffmpegError( int errorCode ) {
		char errorBuffer[AV_ERROR_MAX_STRING_SIZE]{};
		av_strerror( errorCode, errorBuffer, sizeof( errorBuffer ) );
		return QString::fromUtf8( errorBuffer );
	}
	// ====== 智能指针保护ffmpeg资源========
	// 在离开作用域时自动释放资源
	// 对于存在引用计数的资源，注意需要自行调用相关的unref
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
	using AVFormatContextPtr = std::unique_ptr<AVFormatContext, AVFormatContextDeleter>;
	using AVFramePtr = std::unique_ptr<AVFrame, AVFrameDeleter>;
	using AVPacketPtr = std::unique_ptr<AVPacket, AVPacketDeleter>;
	using AVCodecContextPtr = std::unique_ptr<AVCodecContext, AVCodecContextDeleter>;
	// ======================================

	// 中断上下文，用于在读取RTSP流时设置超时和中断标志
	struct InterruptContext {
		int timeoutMs{ DEFAULT_RTSP_READ_TIMEOUT_MS }; // 超时时间
		// 原子变量用于中断回调函数
		std::atomic<bool> pause{ false }; // 外部的中断标志
		std::atomic<bool>stop{false}; // 外部的停止标志，用于解码会话的停止（直接退出循环）
		QElapsedTimer timer; // 计时器

		explicit InterruptContext( int timeout = DEFAULT_RTSP_READ_TIMEOUT_MS )
			: timeoutMs( timeout ) {
			timer.start();  // 开始计时
		}

		// 每次开始一个新的阻塞操作（open / 单次 read）前调用，
		// 让超时以“本次操作”为基准，而不是整个连接期间累计。
		void restart() {
			timer.start();
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


}
/*
Worker解码线程工作流程：
1. controller 调用startDecoding()
2. 	内部执行打开流，查找视频流并更新上下文信息，然后进行解码循环
3. 解码循环中，每读取并解码一个packet，发出frameDecoded()信号，供controller缓存最新帧
4. finished()在最终一次执行循环结束后调用；最终如果执行成功（EOF），发出 finished(true, NoError) 信号；如果出现错误，发出 finished(false, error) 信号交由controller进行处理是否需要重试

3. 如果出现错误，通过reportError()发出错误信号并回填m_lastError（最后通过finished()上报最终的错误）

*/


// Rtsp解码Worker
class RtspDecodeWorker : public QObject {
	Q_OBJECT
public:
	using AVFormatContextPtr = Decode::AVFormatContextPtr;
	using AVFramePtr = Decode::AVFramePtr;
	using AVPacketPtr = Decode::AVPacketPtr;
	using AVCodecContextPtr = Decode::AVCodecContextPtr;
	explicit RtspDecodeWorker( QObject* parent = nullptr );
	explicit RtspDecodeWorker( const RtspConfig& config, QObject* parent = nullptr );
	~RtspDecodeWorker();
	// 设置RTSP URL
	void setRtspUrl( const QString& url );
	// 获取RTSP URL
	QUrl getRtspUrl() const;
	// 设置RTSP配置
	void setRtspConfig( const RtspConfig& config );
	// 设置数据上下文
	void setRtspDataContext( const RtspDataContext& context );
	// 获取数据上下文
	RtspDataContext getRtspDataContext() const;

	// 开始解码：阻塞函数，内部完成 open -> 解码循环 -> 释放资源，
	// 必须在解码线程中调用
	void startDecoding();
	// 停止解码
	// 注意：如果 startDecoding() 正在同一个线程里阻塞，调用stopDecoding()不会停止(queued会阻塞)
	// 跨线程停止用 requestStop()。
	void stopDecoding();
	// 线程安全的停止请求：只写原子变量，可以被任意线程直接调用
	void requestStop();
	// 是否正在解码
	bool isRunning() const;
	// 暂停解码
	void pauseDecoding();
	// 恢复解码
	void resumeDecoding();
	// 回收所有资源
	void releaseResources();
	// 清理所有保存的配置数据并回收资源
	void reset();

signals:
	// 状态发生变化，上层的controller得知（worker 只上报过程状态：Opening/Opened/Playing/Paused，不负责重连等机制）
	void statusChanged( RtspStatus status );
	// 向controller上报解码后的帧，包含帧、索引、pts等信息
	void frameDecoded( const LastestFrameStore& newFrame );	 // 上层Conjtroller调用，获取最新的视频帧结果
	// 一次解码会话结束（startDecoding 返回前必定发出一次）：
	//   clean = true  : 正常 EOF 或被主动停止，重连由 controller 决定是否进行
	//   clean = false : 因错误结束，error 为最后一次错误
	// 统一的会话结束信号
	void finished( bool clean, const RtspOverallError& error );
	void errorOccurred( const RtspOverallError& error ); // 错误发生
	void  dataContextChanged( const RtspDataContext& context ); // RtspDataContext数据上下文发生变化，通知controller保存最新的流信息
	void streamStatusChanged( const RtspStreamStatus& status ); // 按固定间隔(1s)上报拉流统计信息

private:
	// 统一上报错误：只发 errorOccurred，并记录本次执行出错
	void reportError( const RtspOverallError& error );
	// 打开输入（avformat_open_input + find_stream_info + 查找视频流）
	bool openInput();
	// 根据视频流初始化解码器和 packet/frame
	bool initDecoder();
	// 读取 packet 并解码，阻塞直到停止、EOF 或出错
	bool readAndDecodeLoop();
	// 循环接收解码器解码packet后输出的帧
	bool receiveFrames(quint64 readTime);
	// AVFrame -> 独立所有权的 QImage
	QImage convertFrame( const AVFrame* frame );
	// 填充 RtspDataContext
	void recordStreamInfo( const AVStream* stream, const AVCodec* codec );
	// 检查停止请求
	bool isStopRequested() const;
	// 把当前 m_frame 转换成 QImage 并发出信号
	// 检查中断请求
	bool isPaused() const;
	// 包装LastestFrameStore并发出frameDecoded信号
	void emitDecodedFrame(quint64 readTime,quint64 decodedTime);
	// 按固定间隔（1s）汇总并上报统计信息
	void reportStreamStatus();

	RtspDataContext m_dataContext{}; // 数据上下文
	RtspStreamStatus m_streamStatus{}; // 拉流统计信息
	QElapsedTimer m_statusClock; // 统计信息的上报计时
	quint64 m_lastStatusDecodedFrames{ 0 }; // 上次上报时的解码帧数
	quint64 m_lastStatusDisplayedFrames{ 0 }; // 上次上报时的显示帧数
	quint64 m_lastReadTimeUs{ 0 }; // 最近一次成功读取压缩包的时间(us)
	RtspConfig m_config{};			// RTSP配置
	QUrl m_rtspUrl{};				// RTSP URL
	std::mutex m_pauseMutex; // 用来暂停的互斥锁，中断时阻塞解码
	std::condition_variable m_pauseCV; // 用来暂停的条件变量，中断时阻塞解码
	std::atomic<bool> m_stopRequested{ false }; // 停止请求
	std::atomic<bool> m_running{ false }; // 是否正在工作（暂停时同样为true）
	quint64 m_decodedFrameIndex{ 0 };	 // 当前的解码index
	RtspOverallError m_lastError{}; // 用于finished上报错误

	// 中断上下文必须声明在 m_formatContext 之前：
	// 成员按声明逆序析构，这样可以保证 format context 先销毁，
	// interrupt_callback.opaque 不会变成悬空指针。
	Decode::InterruptContext m_interruptContext;

	// SwsContext 不放进 unique_ptr：sws_getCachedContext 会自行释放并重建
	SwsContext* m_swsContext{ nullptr };  // 图像格式转换上下文
	int m_videoStreamIndex{ -1 };

	AVFormatContextPtr m_formatContext{ nullptr };
	AVCodecContextPtr m_codecContext{ nullptr };
	AVPacketPtr m_packet{ nullptr };   // 存储最新的压缩包
	AVFramePtr m_frame{ nullptr };	// 存储最新的帧
};
