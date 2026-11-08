#include "DetectionSession.h"
#include "CpuDetectionFlowFactory.h"
#include "CpuFrameSourceFactory.h"
#include "Logger.h"
#include <QElapsedTimer>

namespace {
	// 预热次数
	constexpr int kDefaultWarmupIterations = 3;
}

DetectionSession::DetectionSession(const DetectorConfig& config)
	: m_config(config)
	, m_mode{ config.effectiveMode() }	// active_mode 优先，未设置时跟随 preferred_mode
{
	// 根据实际生效的模式创建对应的工厂
	switch (m_mode) {
	case FlowMode::CPU:
		m_flowFactory = std::make_unique<CpuDetectionFlowFactory>();
		m_sourceFactory = std::make_unique<CpuFrameSourceFactory>();
		break;
	case FlowMode::GPU:
		// TODO: GPU 流程尚未接入，后续在此创建 GpuDetectionFlowFactory / GpuFrameSourceFactory
		m_flowFactory = nullptr;
		m_sourceFactory = nullptr;
		return;
	}

	// 创建成功后把实际生效的模式写回配置
	m_config.active_mode = m_mode;
	m_detectionFlow = m_flowFactory->createDetectionFlow( m_config );
	if (m_detectionFlow)
		m_warnings = m_detectionFlow->warnings();
	// 如果空指针或者ORT会话创建失败，记录错误与fallback标记，如果触发fallback，直接重建Session
	if (!m_detectionFlow || !m_detectionFlow->isValid()) {
		Logger::instance()->error(QStringLiteral("[DetectionSession]: 检测器初始化失败: ")
			+ (m_detectionFlow ? m_detectionFlow->lastError().errorMessage : QStringLiteral("推理流程创建失败")));
		m_fallbackRequested = true;
		m_lastError = m_detectionFlow ? m_detectionFlow->lastError() : DetectionError(
			QStringLiteral( "[DetectionSession]: 推理流程创建失败" ),
			DetectionErrorCategory::Other, DetectionErrorCode::UnknownError );
		return;
	}

	warmup();
}

// 预热 构造函数内自动调用，日志在会话内部完成
void DetectionSession::warmup() {
	QElapsedTimer timer;
	timer.start();
	const bool warmupSuc = m_detectionFlow->warmup(kDefaultWarmupIterations);
	const quint64 duration = timer.elapsed();
	// 构造/预热产生的警告上报
	// 预热失败时 Detector 会追加 WarmUpFailed 警告，这里重新捕获一次
	m_warnings = m_detectionFlow->warnings();
	for (const DetectorWarning& warning : m_warnings) {
		Logger::instance()->warning(warning.message);
	}

	if (warmupSuc)
		Logger::instance()->info(QStringLiteral("[DetectionSession]: 检测器预热完成，轮数: %1 耗时：%2ms  平均耗时：%3ms")
			.arg(kDefaultWarmupIterations).arg(duration).arg(duration / kDefaultWarmupIterations));
	Logger::instance()->info(QStringLiteral("[DetectionSession]: 检测器就绪（%1）").arg(m_config.effectiveModeText()));
}

// 创建文件帧源（图片/视频），直接转发给帧源工厂
FrameSourcePtr DetectionSession::createFileFrameSource( QStringView filePath, MemoryType memoryType ) {
	return m_sourceFactory->createFileFrameSource( filePath, memoryType );
}

// 创建摄像头帧源(ffmpeg)，直接转发给帧源工厂
FrameSourcePtr DetectionSession::createCameraFrameSource( const QString& nameOrId, const QString& formatName, MemoryType memoryType ) {
	return m_sourceFactory->createCameraFrameSource( nameOrId, formatName, memoryType );
}

// 创建Hik相机帧源，直接转发给帧源工厂（未注册Hik时返回nullptr）
FrameSourcePtr DetectionSession::createHikCameraFrameSource( QStringView serial, CameraTriggerMode mode, MemoryType memoryType ) {
	return m_sourceFactory->createHikCameraFrameSource( serial, mode, memoryType );
}

// 执行推理流程（调用Flow的run，错误记录到 m_lastError 由调用方查询）
bool DetectionSession::runFlow( const FrameHandlePtr& frame, DetectionResult& outResult ) {
	if (!m_detectionFlow) {
		m_lastError = DetectionError(
			QStringLiteral( "[DetectionSession]: 推理流程未创建" ),
			DetectionErrorCategory::Other, DetectionErrorCode::UnknownError );
		return false;
	}
	const bool success = m_detectionFlow->run( frame, outResult );
	// 执行失败记录错误，并标记是否为fallback
	if (!success) {
		m_lastError = m_detectionFlow->lastError();
		m_fallbackRequested = (m_lastError.errorCode == DetectionErrorCode::FallbackRequested);
		// 兜底
		if (!m_lastError.has_error()) {
			m_lastError = DetectionError(
				QStringLiteral( "[DetectionSession]: 推理流程执行失败: " ) + (m_lastError.errorMessage.isEmpty() ? QStringLiteral( "未知错误" ) : m_lastError.errorMessage),
				DetectionErrorCategory::Other, DetectionErrorCode::UnknownError );
		}
	}
	return success;
}

DetectionSession::~DetectionSession() {

}
