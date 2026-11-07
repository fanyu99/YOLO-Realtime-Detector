#include "DetectionSession.h"
#include "CpuDetectionFlowFactory.h"
#include "CpuFrameSourceFactory.h"

DetectionSession::DetectionSession(const DetectorConfig& config,QObject* parent)
	: QObject(parent)
	, m_config(config)
	, m_mode{ config.preferred_mode }
{
	// 根据配置的 FlowMode 创建对应的工厂
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

	m_detectionFlow = m_flowFactory->createDetectionFlow( m_config );
	// 如果空指针或者ORT会话创建失败，触发fallback
	if (!m_detectionFlow || !m_detectionFlow->isValid()) {
		emit fallbackToCPU( m_detectionFlow ? m_detectionFlow->lastError() : DetectionError(
			QStringLiteral( "[DetectionSession]: 推理流程创建失败" ),
			DetectionErrorCategory::Other, DetectionErrorCode::UnknownError ) );
		return;
	}
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

// 执行推理流程（调用Flow的run,如果执行失败且GPU不支持，发送fallback信号）
bool DetectionSession::runFlow( const FrameHandlePtr& frame, DetectionResult& outResult ) {
	if (!m_detectionFlow) {
		emit errorOccurred( DetectionError(
			QStringLiteral( "[DetectionSession]: 推理流程未创建" ),
			DetectionErrorCategory::Other, DetectionErrorCode::UnknownError ) );
		return false;
	}
	const bool success = m_detectionFlow->run( frame, outResult );
	// 执行失败判断是否为fallback
	if (!success) {
		const DetectionError err = m_detectionFlow->lastError();
		if (err.errorCode == DetectionErrorCode::FallbackRequested) {
			emit fallbackToCPU( err );
		}
		else {
			emit errorOccurred( err );
		}
	}
	return success;
}

FlowMode DetectionSession::currentMode()const {
	return m_mode;
}

DetectionSession::~DetectionSession() {

}