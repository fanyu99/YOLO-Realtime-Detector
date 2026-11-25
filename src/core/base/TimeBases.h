#pragma once
#include <QtGlobal>

// 集中时间常量
namespace TimeBases {
	// ---- 单位换算 ----
	inline constexpr quint64 Ms2Ns = 1'000'000ULL;	
	inline constexpr double  Ns2Ms = 1.0e-6;			
	inline constexpr qint64  Ms2Us = 1'000;			
	inline constexpr double  Ms2S = 1'000.0;			

	// ---- 默认值 ----
	inline constexpr int    DefaultReportIntervalMs = 300;		// 性能快照上报间隔
	inline constexpr int    DefaultDecodeReadTimeoutMs = 5000;	// FFmpeg 读取超时
	inline constexpr int    DefaultDecodeConnectTimeoutMs = 5000;	// FFmpeg 连接超时
	inline constexpr int    DefaultFrameNotifyIntervalMs = 16;	// 画面刷新通知限流(~60fps)
	inline constexpr double DefaultVideoWriterFps = 25.0;		// 帧源未知时的录像帧率
	inline constexpr int    DefaultHikFrameTimeoutMs = 1000;	// 海康取帧超时
	inline constexpr int    DefaultHikFrameRetryTimeoutMs = 500;	// 海康取帧重试超时
	inline constexpr int DefaultInfoLogInterval = 30; // 默认的Info级别日志输出间隔
}
