#include "OverlayTextureSource.h"

#include <QImage>
#include <opencv2/core.hpp>

#include "CpuFrameHandle.h"
#include "CudaDevice.h"
#include "FrameConversion.h"
#include "Logger.h"
#if YOLO_HAVE_CUDA
#include <optional>
#include <QSGRendererInterface>
#include <rhi/qrhi.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cuda_runtime.h>
#include <cuda_d3d11_interop.h>
#include <libavutil/hwcontext.h>

#include "CudaDevice.h"
#include "CudaPreprocessKernels.h"
#include "CudaStageTimer.h"
#include "GpuFrameHandle.h"
#endif

namespace {

#if YOLO_HAVE_CUDA

// 原生路径
class NativeSource  {
public:
	~NativeSource () {
		// 仅置空指针，资源清理靠invalidate()
		for (int i = 0; i < 2; ++i) {
			m_cudaRes[i] = nullptr;
			m_qsgTex[i] = nullptr;
			m_rhiTex[i] = nullptr;
			m_textures[i] = nullptr;
		}
	}

	bool available () const { return m_ok; }

	// 懒加载
	bool init ( QQuickWindow* window, IPerformanceMonitor* monitor ) {
		m_monitor = monitor;
		auto* rif = window->rendererInterface ();
		if (rif->graphicsApi () != QSGRendererInterface::Direct3D11) {
			fail ( QStringLiteral ( "RHI 后端不是 D3D11，原生互操作不可用" ) );
			return false;
		}
		auto* d3d = static_cast<ID3D11Device*>( rif->getResource (window,QSGRendererInterface::DeviceResource) );
		if (!d3d) {
			fail ( QStringLiteral ( "无法从渲染接口取得 D3D11 device" ) );
			return false;
		}
		if (!CudaDevice::instance ().isValid ()) {
			fail ( QStringLiteral ( "CUDA 设备未就绪" ) );
			return false;
		}
		m_cudaDeviceId = CudaDevice::instance ().deviceId ();
		m_stream = CudaDevice::instance ().stream ();
		if (m_monitor)
			m_convertTimer.emplace ( m_stream, m_monitor, PerformanceStage::ConvertKernel );
		// 适配器核对
		if (!luidMatches ( d3d, m_cudaDeviceId )) {
			fail ( QStringLiteral ( "D3D11 适配器与 CUDA 设备 LUID 不匹配" ) );
			return false;
		}
		if (cudaSetDevice ( m_cudaDeviceId ) != cudaSuccess) {
			fail ( QStringLiteral ( "渲染线程 cudaSetDevice 失败" ) );
			return false;
		}
		m_d3dDevice = d3d;
		m_ok = true;
		Logger::instance ()->success ( QStringLiteral (
			"[OverlayTextureSource]: CUDA-D3D11 互操作就绪(设备 %1)，GPU 帧走零拷贝叠加" )
			.arg ( m_cudaDeviceId ) );
		return true;
	}

	// 创建纹理并注册到CUDA资源中
	QSGTexture* textureFor ( const FrameHandlePtr& frame, QQuickWindow* window ) {
		if (!m_ok)
			return nullptr;
		const auto gpu = std::dynamic_pointer_cast<GpuFrameHandle> ( frame );
		if (!gpu || !gpu->isValid ()) {
			failOnce ( QStringLiteral ( "快照帧不是有效的 GPU 句柄" ) );
			return nullptr;
		}
		const int w = gpu->width (), h = gpu->height ();
		if (w <= 0 || h <= 0) {
			failOnce ( QStringLiteral ( "GPU 帧尺寸非法" ) );
			return nullptr;
		}
		if (!ensureSize ( w, h, window ))
			return nullptr;

		m_slot ^= 1;	
		const int slot = m_slot;
		cudaSetDevice ( m_cudaDeviceId );
		if (cudaGraphicsMapResources ( 1, &m_cudaRes[slot], m_stream ) != cudaSuccess) {
			failOnce ( QStringLiteral ( "CUDA map D3D11 纹理失败" ) );
			return nullptr;
		}
		bool ok = false;
		CudaKernelError kerr{};
		cudaSurfaceObject_t surf = 0;
		cudaArray_t array = nullptr;
		if (cudaGraphicsSubResourceGetMappedArray ( &array, m_cudaRes[slot], 0, 0 ) != cudaSuccess) {
			failOnce ( QStringLiteral ( "CUDA 取映射数组失败" ) );
		} else {
			cudaResourceDesc resDesc{};
			resDesc.resType = cudaResourceTypeArray;
			resDesc.res.array.array = array;
			if (cudaCreateSurfaceObject ( &surf, &resDesc ) != cudaSuccess) {
				failOnce ( QStringLiteral ( "CUDA 创建 surface 失败" ) );
			} else {
				if (m_convertTimer)
					m_convertTimer->begin ();
				const PixelFormat fmt = gpu->pixelFormat ();
				if (fmt == PixelFormat::NV12) {
					const auto p0 = gpu->plane ( 0 );
					const auto p1 = gpu->plane ( 1 );
					ok = launchNv12ToBgraKernel ( p0.data, p0.stride, p1.data, p1.stride, w, h,
						surf, readColorRange ( gpu.get () ), m_stream, &kerr );
				} else if (fmt == PixelFormat::BGR24) {
					const auto p0 = gpu->plane ( 0 );
					ok = launchBgr24ToBgraKernel ( p0.data, p0.stride, w, h,
						surf, m_stream, &kerr );
				} else {
					failOnce ( QStringLiteral ( "暂不支持的 GPU 帧格式: " ) + pixelFormatToString ( fmt ) );
				}
				if (m_convertTimer)
					m_convertTimer->markStop ();
			}
		}
		cudaStreamSynchronize ( m_stream );
		if (m_convertTimer)
			m_convertTimer->waitForResult ();	// 上面的同步已完成，读取事件无额外等待
		// 等到完成了再析构
		if (surf)
			cudaDestroySurfaceObject ( surf );
		cudaGraphicsUnmapResources ( 1, &m_cudaRes[slot], m_stream );
		if (!ok) {
			if (kerr.hasError)
				failOnce ( QStringLiteral ( "显示转换 kernel 失败: " ) + QString::fromUtf8 ( kerr.message ) );
			return nullptr;
		}
		// 返回缓存
		return m_qsgTex[slot];
	}

	// sceneGraphInvalidated 时由渲染线程调用
	void invalidate () {
		if (!m_ok && m_size.isEmpty ())
			return;		
		releaseTextures ();
		m_ok = false;
	}

private:
	// 确保纹理尺寸正确
	bool ensureSize ( int w, int h, QQuickWindow* window ) {
		if (m_size == QSize ( w, h ))
			return m_ok;
		releaseTextures ();
		auto* rhi = static_cast<QRhi*>( window->rendererInterface ()->getResource (window,QSGRendererInterface::RhiResource) );
		if (!rhi) {
			fail ( QStringLiteral ( "无法取得 QRhi" ) );
			return false;
		}
		for (int i = 0; i < 2; ++i) {
			D3D11_TEXTURE2D_DESC d{};
			d.Width = static_cast<UINT>( w );
			d.Height = static_cast<UINT>( h );
			d.MipLevels = 1;
			d.ArraySize = 1;
			d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			d.SampleDesc.Count = 1;
			d.Usage = D3D11_USAGE_DEFAULT;
			// CUDA 要写这张纹理：必须有可写绑定（RENDER_TARGET），只给 SHADER_RESOURCE 拿不到写权限
			d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
			if (FAILED ( m_d3dDevice->CreateTexture2D ( &d, nullptr, &m_textures[i] ) )) {
				fail ( QStringLiteral ( "D3D11 纹理创建失败(%1x%2)" ).arg ( w ).arg ( h ) );
				return false;
			}
			// SurfaceLoadStore：允许通过 surface object 写入（只读注册会拿不到写权限）
			if (cudaGraphicsD3D11RegisterResource ( &m_cudaRes[i], m_textures[i],
				cudaGraphicsRegisterFlagsSurfaceLoadStore ) != cudaSuccess) {
				fail ( QStringLiteral ( "CUDA 注册 D3D11 纹理失败" ) );
				return false;
			}
			m_rhiTex[i] = rhi->newTexture ( QRhiTexture::BGRA8, QSize ( w, h ), 1, QRhiTexture::Flag ( 0 ) );
			m_rhiTex[i]->create ();
			const QRhiTexture::NativeTexture native{
				reinterpret_cast<quintptr>( m_textures[i] ), 0 };
			if (!m_rhiTex[i]->createFrom ( native )) {
				fail ( QStringLiteral ( "QRhiTexture 包装原生纹理失败" ) );
				return false;
			}
			// 缓存包装：之后每帧直接返回它，不再新建（见 textureFor 处的所有权说明）
			m_qsgTex[i] = window->createTextureFromRhiTexture ( m_rhiTex[i] );
			if (!m_qsgTex[i]) {
				fail ( QStringLiteral ( "QSGTexture 包装 RHI 纹理失败" ) );
				return false;
			}
		}
		m_size = QSize ( w, h );
		return m_ok;
	}

	// 释放纹理
	// 先注销CUDA（要求 D3D 纹理仍有效）
	// 再删 QSGTexture 包装（连带释放 QRhiTexture 及其 SRV）
	// 最后 Release D3D 纹理
	void releaseTextures () {
		cudaSetDevice ( m_cudaDeviceId );
		for (int i = 0; i < 2; ++i) {
			if (m_cudaRes[i]) {
				cudaGraphicsUnregisterResource ( m_cudaRes[i] );
				m_cudaRes[i] = nullptr;
			}
			if (m_qsgTex[i]) {
				// 包装持有底层 QRhiTexture 的所有权：删包装即释放 QRhiTexture
				delete m_qsgTex[i];
				m_qsgTex[i] = nullptr;
				m_rhiTex[i] = nullptr;	// 所有权已随包装释放，不能再单独 delete
			}
			else if (m_rhiTex[i]) {
				// 包装创建失败时才由本类释放
				delete m_rhiTex[i];
				m_rhiTex[i] = nullptr;
			}
			if (m_textures[i]) {
				m_textures[i]->Release ();
				m_textures[i] = nullptr;
			}
		}
		m_size = QSize ();
	}

	// 比对 LUID，确保 D3D11 适配器与 CUDA 设备一致
	static bool luidMatches ( ID3D11Device* d3d, int cudaDeviceId ) {
		IDXGIDevice* dxgi = nullptr;
		if (FAILED ( d3d->QueryInterface ( __uuidof(IDXGIDevice), reinterpret_cast<void**>( &dxgi ) ) ))
			return false;
		IDXGIAdapter* adapter = nullptr;
		DXGI_ADAPTER_DESC desc{};
		bool matched = false;
		if (SUCCEEDED ( dxgi->GetAdapter ( &adapter ) ) && adapter) {
			if (SUCCEEDED ( adapter->GetDesc ( &desc ) )) {
				cudaDeviceProp prop{};
				if (cudaGetDeviceProperties ( &prop, cudaDeviceId ) == cudaSuccess)
					matched = memcmp ( &desc.AdapterLuid, prop.luid, sizeof ( LUID ) ) == 0;
			}
			adapter->Release ();
		}
		dxgi->Release ();
		return matched;
	}

	// 色彩范围
	static CudaColorRange readColorRange ( const GpuFrameHandle* gpu ) {
		const AVFrame* av = gpu ? gpu->constFrame () : nullptr;
		if (av && av->color_range == AVCOL_RANGE_JPEG)
			return CudaColorRange::Full;
		return CudaColorRange::Limited;
	}

	void fail ( const QString& reason ) {
		m_ok = false;
		Logger::instance ()->warning ( QStringLiteral ( "[OverlayTextureSource]: " ) + reason
			+ QStringLiteral ( "，GPU 帧将回退上传路径" ) );
	}
	// 逐帧路径上的失败只报一次，避免视频流刷屏
	void failOnce ( const QString& reason ) {
		if (!m_failReported) {
			m_failReported = true;
			Logger::instance ()->warning ( QStringLiteral ( "[OverlayTextureSource]: " ) + reason
				+ QStringLiteral ( "（仅报告一次）" ) );
		}
	}

	QSize m_size;								// 当前纹理尺寸
	ID3D11Device* m_d3dDevice = nullptr;		// 借用设备指针
	ID3D11Texture2D* m_textures[2]{};			// 双缓冲BGRA8
	cudaGraphicsResource_t m_cudaRes[2]{};		// CUDA 互操作句柄
	QRhiTexture* m_rhiTex[2]{};					// 包装原生纹理，双缓冲复用（所有权在创建后交给 QSGTexture 包装）
	QSGTexture* m_qsgTex[2]{};					// 缓存的包装纹理，每帧直接复用（节点 ownsTexture=false）
	cudaStream_t m_stream{};					// 借用CudaDevice的统一的stream
	std::optional<CudaStageTimer> m_convertTimer;	// 显示转换 kernel 的设备耗时
	IPerformanceMonitor* m_monitor = nullptr;		// 借用监测器，用于转换 kernel 计时
	int m_cudaDeviceId = -1;
	int m_slot = 0;	   // 当前的槽位，后续进行交换（异或）
	bool m_ok = false;
	bool m_failReported = false;
};

#endif 

} 

struct OverlayTextureSource::Impl {
	// 无CUDA就为空
#if YOLO_HAVE_CUDA
	NativeSource native;
	bool nativeInited = false;	// 首个 GPU 帧时才尝试 init（渲染线程 API）
#endif
	IPerformanceMonitor* monitor = nullptr;	// 借用监测器，用于显示转换 kernel 计时
	// Upload，节点不拥有内存，纹理生命周期由本类管理，每帧新建包装，上一帧交给deleteLater
	QSGTexture* lastUpload = nullptr;

	QSGTexture* uploadFor ( const FrameHandlePtr& frame, QQuickWindow* window ) {
		if (!frame || frame->memoryType () != MemoryType::CPU)
			return nullptr;
		const cv::Mat mat = toCvMat ( frame );
		if (mat.empty () || mat.type () != CV_8UC3)
			return nullptr;
		const QImage view ( mat.data, mat.cols, mat.rows, static_cast<int>(mat.step),
			QImage::Format_BGR888 );
		QSGTexture* texture = window->createTextureFromImage ( view, QQuickWindow::TextureIsOpaque );
		if (!texture)
			return nullptr;
		if (lastUpload)
			lastUpload->deleteLater ();	// 渲染线程清理队列
		lastUpload = texture;
		return texture;
	}
};

OverlayTextureSource::OverlayTextureSource ( IPerformanceMonitor* monitor )
	: m_impl ( std::make_unique<Impl> () ) {
	m_impl->monitor = monitor;
}

OverlayTextureSource::~OverlayTextureSource () = default;

void OverlayTextureSource::setMonitor ( IPerformanceMonitor* monitor ) {
	m_impl->monitor = monitor;
}

QSGTexture* OverlayTextureSource::textureFor ( const FrameHandlePtr& frame, QQuickWindow* window ) {
	if (!frame)
		return nullptr;
#if YOLO_HAVE_CUDA
	if (frame->memoryType () == MemoryType::GPU) {
		if (!m_impl->nativeInited) {
			m_impl->nativeInited = true;
			m_impl->native.init ( window, m_impl->monitor ); // 如果初始化失败，每帧返回nullptr
		}
		if (m_impl->native.available ())
			return m_impl->native.textureFor ( frame, window );
		return nullptr;
	}
#endif
	// CPU帧上传
	return m_impl->uploadFor ( frame, window );
}

// 原生的互操作是否可用
bool OverlayTextureSource::nativeInteropAvailable () const {
#if YOLO_HAVE_CUDA
	return m_impl->nativeInited && m_impl->native.available ();
#else
	return false;
#endif
}

void OverlayTextureSource::invalidate () {
#if YOLO_HAVE_CUDA
	if (m_impl->nativeInited)
		m_impl->native.invalidate ();
#endif
	// Upload 路径渲染线程清理队列
	if (m_impl->lastUpload) {
		m_impl->lastUpload->deleteLater ();
		m_impl->lastUpload = nullptr;
	}
}
