#pragma once
#include <QQuickWindow>
#include <QSGTexture>
#include <memory>

#include "FrameHandle.h"
#include "IPerformanceMonitor.h"

// 帧像素数据纹理源
//   Upload CPU 帧经 QImage 上传 到Overlay
//   Native  GPU 帧经 CUDA和D3D11交互零拷贝
// 渲染线程调用
// 抽象接口
class IFrameTextureSource {
public:
	virtual ~IFrameTextureSource () = default;
	// 返回的纹理指针由调用方接管所有权；
	// 无法显示时返回 nullptr
	virtual QSGTexture* textureFor ( const FrameHandlePtr& frame, QQuickWindow* window ) = 0;
	// GPU 帧的互操作是否可用，用来判断是否选择CPU上传还是直接GPU
	virtual bool nativeInteropAvailable () const { return false; }
	// 渲染线程清理资源，在窗口发送invalidate信号时触发
	virtual void invalidate () {}
	virtual void setMonitor ( IPerformanceMonitor* monitor ) {};
};
using FrameTextureSourcePtr = std::shared_ptr<IFrameTextureSource>;

class QQuickWindow;
class IPerformanceMonitor;

// Overlay纹理源，按照帧源的内存类型选择 Upload 或 Native
class OverlayTextureSource final : public IFrameTextureSource {
public:
	explicit OverlayTextureSource ( IPerformanceMonitor* monitor = nullptr );
	~OverlayTextureSource () override;

	QSGTexture* textureFor ( const FrameHandlePtr& frame, QQuickWindow* window ) override;
	bool nativeInteropAvailable () const override;
	void invalidate () override;

	// 注入监测器，用于统计显示转换 kernel 的耗时。
	// 必须在首个 GPU 帧触发 native 初始化之前调用。
	void setMonitor ( IPerformanceMonitor* monitor ) override;

private:
	OverlayTextureSource ( const OverlayTextureSource& ) = delete;
	OverlayTextureSource& operator=( const OverlayTextureSource& ) = delete;
	struct Impl;
	std::unique_ptr<Impl> m_impl;	// IMPLE
};
