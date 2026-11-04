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
#include "FfmpegDecoder.h"

/*
Worker解码线程工作流程：
1. controller 调用startDecoding()
2. 	内部执行打开流，查找视频流并更新上下文信息，然后进行解码循环
3. 解码循环中，每读取并解码一个packet，发出FrameDecoded()信号，供controller缓存最新帧
4. Finished()在最终一次执行循环结束后调用；最终如果执行成功（EOF），发出 Finished(true, NoError) 信号；如果出现错误，发出 Finished(false, error) 信号交由controller进行处理是否需要重试

3. 如果出现错误，通过reportError()发出错误信号并回填m_lastError（最后通过Finished()上报最终的错误）

*/


// Rtsp解码Worker
class RtspDecodeWorker : public QObject {
	Q_OBJECT
public:
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
	void StatusChanged( RtspStatus status );
	// 向controller上报解码后的帧，包含帧、索引、pts等信息
	void FrameDecoded( const LastestFrameStore& newFrame );	 // 上层Conjtroller调用，获取最新的视频帧结果
	// 一次解码会话结束（startDecoding 返回前必定发出一次）：
	//   clean = true  : 正常 EOF 或被主动停止，重连由 controller 决定是否进行
	//   clean = false : 因错误结束，error 为最后一次错误
	// 统一的会话结束信号
	void Finished( bool clean, const RtspOverallError& error );
	void ErrorOccurred( const RtspOverallError& error ); // 错误发生
	void DataContextChanged( const RtspDataContext& context ); // RtspDataContext数据上下文发生变化，通知controller保存最新的流信息
	void StreamStatusChanged( const RtspStreamStatus& status ); // 按固定间隔(1s)上报拉流统计信息

private:
	// 统一上报错误：只发 errorOccurred，并记录本次执行出错
	void reportError( const RtspOverallError& error );
	// 读取 packet 并解码，阻塞直到停止、EOF 或出错
	bool readAndDecodeLoop();
	// AVFrame -> 独立所有权的 QImage
	QImage convertFrame( const AVFrame* frame );
	// 填充 RtspDataContext
	void recordStreamInfo( const StreamInfo& info );
	// 检查停止请求
	bool isStopRequested() const;
	// 把当前 m_frame 转换成 QImage 并发出信号
	// 检查中断请求
	bool isPaused() const;
	// 包装LastestFrameStore并发出frameDecoded信号
	void emitDecodedFrame(const AVFrame* frame,quint64 readTime,quint64 decodedTime);
	// 按固定间隔（1s）汇总并上报统计信息
	void reportStreamStatus();
	// 解码错误 -> Rtsp错误码
	RtspOverallError toRtspError( const DecodeError& error ) const;

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

	// SwsContext 不放进 unique_ptr：sws_getCachedContext 会自行释放并重建
	SwsContext* m_swsContext{ nullptr };  // 图像格式转换上下文

	FfmpegDecoder m_decoder; // 通用ffmpeg解码器
};
