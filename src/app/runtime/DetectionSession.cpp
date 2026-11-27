#include "DetectionSession.h"
#include "CpuDetectionFlowFactory.h"
#include "CpuFrameSourceFactory.h"
#include "CpuFrameHandle.h"
#ifdef YOLO_HAVE_CUDA
#include "GpuFrameSourceFactory.h"
#include "GpuDetectionFlowFactory.h"
#include "GpuImageFrameHandle.h"
#include "CudaDevice.h"
#endif
#include "Logger.h"
#include "PluginManager.h"
#include "ScopedStageTimer.h"
#include "TimeBases.h"

namespace {
	// 预热次数
	constexpr int DefaultWarmupIterations = 3;
}

DetectionSession::DetectionSession ( const DetectorConfig& config, IPerformanceMonitor* monitor )
	: m_config ( config )
	, m_monitor ( monitor )
	, m_mode{ config.effectiveMode () }	// active_mode 优先，未设置时跟随 preferred_mode
{
	// 根据实际生效的模式创建对应的工厂
	switch (m_mode) {
	case FlowMode::CPU: {
		m_flowFactory = std::make_unique<CpuDetectionFlowFactory> ();
		m_sourceFactory = std::make_unique<CpuFrameSourceFactory> ();
		break;
	}
	case FlowMode::GPU: {
#ifdef YOLO_HAVE_CUDA
		m_flowFactory = std::make_unique<GpuDetectionFlowFactory> ();
		m_sourceFactory = std::make_unique<GpuFrameSourceFactory> ();
		if (CudaDevice::instance ().isValid ())
			m_mode = FlowMode::GPU; // GPU模式
#else
		m_mode = FlowMode::CPU;
		m_flowFactory = std::make_unique<CpuDetectionFlowFactory> ();
		m_sourceFactory = std::make_unique<CpuFrameSourceFactory> ();
#endif
		break;
	}
	}

	// 创建成功后把实际生效的模式写回配置
	m_config.active_mode = m_mode;

	// 把已加载的帧源插件注册到工厂 插件缺失时对应 pluginId 创建会返回空
	if (PluginManager* manager = PluginManager::instance ()) {
		for (const QString& id : manager->enumeratePluginIds ()) {
			if (auto* plugin = manager->getFrameSourcePlugin ( id ))
				m_sourceFactory->appendFrameSourcePlugin ( id, plugin->getFrameSourceCallback () );
		}
	}
	m_detectionFlow = m_flowFactory->createDetectionFlow ( m_config, m_monitor );
	if (m_detectionFlow)
		m_warnings = m_detectionFlow->warnings ();
	// 如果空指针或者ORT会话创建失败，记录错误与fallback标记，如果触发fallback，直接重建Session
	if (!m_detectionFlow || !m_detectionFlow->isValid ()) {
		Logger::instance ()->error ( QStringLiteral ( "[DetectionSession]: 检测器初始化失败: " )
			+ (m_detectionFlow ? m_detectionFlow->lastError ().errorMessage : QStringLiteral ( "推理流程创建失败" )) );
		m_fallbackRequested = m_detectionFlow ? m_detectionFlow->lastError ().errorCode == DetectionErrorCode::FallbackRequested : false; // 判断是否需要fallback
		m_lastError = m_detectionFlow ? m_detectionFlow->lastError () : DetectionError (
			QStringLiteral ( "[DetectionSession]: 推理流程创建失败" ),
			DetectionErrorCategory::Other, DetectionErrorCode::UnknownError );
		return;
	}

	warmup ();
}

// 预热 构造函数内自动调用，日志在会话内部完成
void DetectionSession::warmup () {
	// 记录预热轮数（会话级）
	if (m_monitor)
		m_monitor->setWarmupIterations ( DefaultWarmupIterations );
	// 记录预热阶段性能数据
	quint64 warmupNs=0;
	ScopedStageTimer timer ( m_monitor, PerformanceStage::Warmup,&warmupNs );
	const bool warmupSuc = m_detectionFlow->warmup ( DefaultWarmupIterations );
	timer.end ();
	// 构造/预热产生的警告上报
	m_warnings = m_detectionFlow->warnings ();
	for (const DetectorWarning& warning : m_warnings) {
		Logger::instance ()->warning ( warning.message );
	}

	if (warmupSuc)
		Logger::instance ()->info ( QStringLiteral ( "[DetectionSession]: 检测器预热完成，轮数: %1，总耗时: %2 ms" )
			.arg ( DefaultWarmupIterations ).arg (static_cast<double>( warmupNs) * TimeBases::Ns2Ms) ) ;
	Logger::instance ()->info ( QStringLiteral ( "[DetectionSession]: 检测器就绪（%1）" ).arg ( m_config.effectiveModeText () ) );
}

// 创建图片帧源（cv::imread，固定 CPU 内存）
FrameSourcePtr DetectionSession::createImageFrameSource ( QStringView filePath ) {
	return m_sourceFactory->createImageFrameSource ( filePath );
}

// 创建视频帧源（ffmpeg）
FrameSourcePtr DetectionSession::createVideoFrameSource ( QStringView filePath ) {
	return m_sourceFactory->createVideoFrameSource ( filePath, getMemoryType () );
}

// 创建摄像头帧源(ffmpeg)
FrameSourcePtr DetectionSession::createCameraFrameSource ( const QString& nameOrId, const QString& formatName ) {
	return m_sourceFactory->createCameraFrameSource ( nameOrId, formatName, getMemoryType () );
}

// 通过帧源插件创建帧源，插件未注册时返回nullptr
FrameSourcePtr DetectionSession::createFrameSourceByPlugin ( const QString& pluginId, const FrameSourceDeviceInfo& deviceInfo ) {
	return m_sourceFactory->createFrameSourceByPlugin ( pluginId, deviceInfo );
}

// 执行推理流程（调用Flow的run）
bool DetectionSession::runFlow ( FrameHandlePtr frame, DetectionResult& outResult ) {
	if (!m_detectionFlow) {
		m_lastError = DetectionError (
			QStringLiteral ( "[DetectionSession]: 推理流程未创建" ),
			DetectionErrorCategory::Other, DetectionErrorCode::UnknownError );
		outResult.error = m_lastError;
		return false;
	}
	// GPU会话收到CPU帧（单图imread产出）：上传到显存后继续走GPU流程
	if (frame->memoryType () == MemoryType::CPU && getMemoryType () == MemoryType::GPU) {
#ifdef YOLO_HAVE_CUDA
		const auto cpuHandle = std::dynamic_pointer_cast<CpuFrameHandle>(frame);
		if (!cpuHandle || cpuHandle->constMat ().empty ()) {
			m_lastError = DetectionError (
				QStringLiteral ( "[DetectionSession]: CPU帧转GPU上传失败: 帧句柄无效或图像为空" ),
				DetectionErrorCategory::Image, DetectionErrorCode::InvalidImage );
			outResult.error = m_lastError;
			return false;
		}
		// 单图路径的显存上传计时
		std::shared_ptr<GpuImageFrameHandle> gpuHandle;
		{
			ScopedStageTimer uploadTimer ( m_monitor, PerformanceStage::Frame2Gpu );
			gpuHandle = std::make_shared<GpuImageFrameHandle> ( cpuHandle->constMat (), frame->frameIndex () );
		}
		if (!gpuHandle->isValid ()) {
			m_lastError = DetectionError (
				QStringLiteral ( "[DetectionSession]: CPU帧上传GPU显存失败，触发fallback到CPU模式；详细信息：显存数据是否为空：" ) + (gpuHandle->dataEmpty () ? QStringLiteral ( "是" ) : QStringLiteral ( "否" ))
				+ QStringLiteral ( ", width:" ) + QString::number ( gpuHandle->width () )
				+ QStringLiteral ( ", height:" ) + QString::number ( gpuHandle->height () )
				+ QStringLiteral ( ", frameIndex:" ) + QString::number ( frame->frameIndex () )
				+ QStringLiteral ( ", pixelFormat:" ) + pixelFormatToString ( gpuHandle->pixelFormat () )
				+ QStringLiteral ( " ,stride " ) + QString::number ( gpuHandle->stride () ),
				DetectionErrorCategory::CUDA, DetectionErrorCode::FallbackRequested );
			outResult.error = m_lastError;
			return false;
		}
		frame = std::move ( gpuHandle ); // 后续流程持有GPU帧
#else
		m_lastError = DetectionError (
			QStringLiteral ( "[DetectionSession]: 非CUDA构建下GPU会话不应收到CPU帧" ),
			DetectionErrorCategory::Config, DetectionErrorCode::FallbackRequested );
		outResult.error = m_lastError;
		return false;
#endif
	}
	const bool success = m_detectionFlow->run ( frame, outResult );
	// 执行失败记录错误，并标记是否为fallback
	if (!success) {
		m_lastError = m_detectionFlow->lastError ();
		m_fallbackRequested = (m_lastError.errorCode == DetectionErrorCode::FallbackRequested);
		// 兜底
		if (!m_lastError.has_error ()) {
			m_lastError = DetectionError (
				QStringLiteral ( "[DetectionSession]: 推理流程执行失败: " ) + (m_lastError.errorMessage.isEmpty () ? QStringLiteral ( "未知错误" ) : m_lastError.errorMessage),
				DetectionErrorCategory::Other, DetectionErrorCode::UnknownError );
		}
	}
	return success;
}

// 获取内存类型
MemoryType DetectionSession::getMemoryType ()const {
	switch (m_config.effectiveMode ()) {
	case FlowMode::CPU:
		return MemoryType::CPU;
	case FlowMode::GPU:
		return MemoryType::GPU;
	default:
		return MemoryType::CPU;
	}
}