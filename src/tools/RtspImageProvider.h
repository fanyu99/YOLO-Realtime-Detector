#pragma once
#include <QQuickImageProvider>
#include <QImage>
#include <QMutex>
#include "RtspTypes.h"

// Rtsp 图像提供者，用来把解码线程产出的最新帧提供给 QML 的 Image 元素
/*
实现：
1. 线程安全地保存最新解码帧（QML 请求可能来自非 GUI 线程）
2. 响应 QML 的 image://rtsp/... 请求，返回最新帧
3. 支持按 QML 端的 requestedSize 等比缩放，减少拷贝开销
*/
class RtspImageProvider : public QQuickImageProvider {
public:
	RtspImageProvider();

	// QML 端通过 Image { source: "image://rtsp/live" } 触发。
	// id 仅用于 QML 端缓存绕过（如 "live?123"），此处不解析
	QImage requestImage( const QString& id, QSize* size, const QSize& requestedSize ) override;

	// 更新最新帧，可由任意线程调用（一般由 Controller 的 FrameUpdated 驱动）
	void updateFrame( const LastestFrameStore& frame );
	void updateFrame( const QImage& frame );
	// 清空缓存帧（停止/出错时调用）
	void clear();
	// 是否已有可用帧
	bool hasFrame() const;
	// 当前帧序号，可用于 QML 端构造缓存绕过参数
	quint64 frameIndex() const;

private:
	mutable QMutex m_mutex; // 保护 m_latestFrame / m_frameIndex
	QImage m_latestFrame{}; // 最新帧（QImage 隐式共享，拷贝代价低）
	quint64 m_frameIndex{ 0 }; // 帧序号
};
