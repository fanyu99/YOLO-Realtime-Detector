#include  "GpuDetectionFlow.h"
#include "Logger.h"
#include <QElapsedTimer>

GpuDetectionFlow::GpuDetectionFlow( const DetectorConfig& config )
	: m_config( config )
	, m_detector( config )
	, m_preprocessor()
	,m_postprocessor()
{
	m_lastError = m_detector.lastError();
}

// 预热
bool   GpuDetectionFlow::warmup( int iterations ) {
	if (!m_detector.isValid()) {
		m_lastError = m_detector.lastError();
		return false;
	}
	return m_detector.warmup( iterations );
}

// 	完整的处理流程：预处理 + 推理 + 后处理
bool GpuDetectionFlow::run( const FrameHandlePtr& handle, DetectionResult& outResult ) {
	outResult = DetectionResult(); // 复位
	m_lastError = DetectionError(); 
	// 校验各个组件是否有效
	if (!m_detector.isValid()) {
		m_lastError = m_detector.lastError();
		if (!m_lastError.has_error()) {
			m_lastError = DetectionError(
				QStringLiteral( "[GpuDetectionFlow]: GPU推理器未初始化/不支持CUDA/会话无效" ),
				DetectionErrorCategory::CUDA,
				DetectionErrorCode::FallbackRequested );
		}
		outResult.error = m_lastError;
		return false;
	}
	if (m_activeMode != FlowMode::GPU) {
		m_lastError = DetectionError(
			QStringLiteral( "[GpuDetectionFlow]: 当前流程的实际计算模式不是GPU" ),
			DetectionErrorCategory::Config,
			DetectionErrorCode::FallbackRequested );
		outResult.error = m_lastError;
		return false;
	}
	if (!handle) {
		m_lastError = DetectionError(
			QStringLiteral( "[GpuDetectionFlow]: 输入的帧句柄为空" ),
			DetectionErrorCategory::Preprocess,
			DetectionErrorCode::PreprocessFailed );
		outResult.error = m_lastError;
		return false;
	}
	// 预处理
	auto preprocessedOpt = m_preprocessor.preprocess( handle, m_detector.inputWidth(), m_detector.inputHeight() );
	if (!preprocessedOpt.has_value()) {
		m_lastError = m_preprocessor.lastError();
		if (!m_lastError.has_error()) { // 兜底错误
			m_lastError = DetectionError( QStringLiteral( "[GpuDetectionFlow]: 预处理失败" ), DetectionErrorCategory::Preprocess, DetectionErrorCode::PreprocessFailed );
		}
		outResult.error = m_lastError;
		return false;
	}
	const auto& preprocessed = preprocessedOpt.value();
	// 推理
	std::vector<float> output_data;
	std::vector<int64_t> output_shape;
	QElapsedTimer timer;
	timer.start();
	if (!m_detector.infer( preprocessed, output_data, output_shape )) {
		m_lastError = m_detector.lastError();
		if (!m_lastError.has_error()) { // 兜底错误
			m_lastError = DetectionError( QStringLiteral( "[GpuDetectionFlow]: 推理失败" ), DetectionErrorCategory::Detection, DetectionErrorCode::DetectionFailed );
		}
		outResult.error = m_lastError;
		return false;
	}
	const quint64 duration = timer.elapsed();
	// 校验结果
	if (output_shape.size() < 3) {
		m_lastError = DetectionError(
			QStringLiteral( "[GpuDetectionFlow]: 推理输出维度不合法: " ) + QString::number( output_shape.size() ),
			DetectionErrorCategory::Detection,
			DetectionErrorCode::DetectionFailed );
		outResult.error = m_lastError;
		return false;
	}
	const int num_classes = static_cast<int>(output_shape[1]) - 4;
	const int num_anchors = static_cast<int>(output_shape[2]);
	if (num_classes <= 0 || num_anchors <= 0) {
		m_lastError = DetectionError(
			QStringLiteral( "[GpuDetectionFlow]: 推理输出维度不合法: num_classes=" ) + QString::number( num_classes ) + QStringLiteral( ", num_anchors=" ) + QString::number( num_anchors ),
			DetectionErrorCategory::Detection,
			DetectionErrorCode::DetectionFailed );
		outResult.error = m_lastError;
		return false;
	}
	// 进行后处理
	std::vector<Box>boxes;
	if (!m_postprocessor.postProcess( output_data.data(), num_classes, num_anchors,
		preprocessedOpt->letterboxInfo, m_config.conf_threshold, m_config.iou_threshold, boxes ) ) {
		m_lastError = m_postprocessor.lastError();
		if (!m_lastError.has_error()) { // 兜底错误
			m_lastError = DetectionError( QStringLiteral( "[GpuDetectionFlow]: 后处理失败" ), DetectionErrorCategory::Postprocess, DetectionErrorCode::PostprocessFailed );
		}
		outResult.error = m_lastError;
		return false;	
	}
	// 最后总结结果
	outResult.boxes = QVector<Box>(boxes.begin(), boxes.end());
	outResult.duration = duration;
	Logger::instance()->infoWithInterval( QStringLiteral( "[GpuDetectionFlow]: 检测到%1个目标，推理耗时: %2ms" )
		.arg( outResult.boxes.size() ).arg( duration ) );
	return true;

}

bool GpuDetectionFlow::isValid() const {
	return m_detector.isValid();
}

DetectionError  GpuDetectionFlow::lastError() const {
	return m_lastError;
}

const QVector<DetectorWarning>& GpuDetectionFlow::warnings() const {
	return m_detector.warnings();
}