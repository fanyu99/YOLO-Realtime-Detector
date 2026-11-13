#include "CpuDetectionFlow.h"
#include <QString>
#include <QElapsedTimer>
#include <vector>
#include "Logger.h"

CpuDetectionFlow::CpuDetectionFlow( const DetectorConfig& config )
	: m_detector( config ),
	m_config( config ) {
}

FlowMode CpuDetectionFlow::activeMode() const {
	return m_activeMode;
}
FlowMode CpuDetectionFlow::preferredMode() const {
	return m_preferredMode;
}

// 检测器会话是否构造成功
bool CpuDetectionFlow::isValid() const {
	return m_detector.isValid();
}
// 最近一次错误：优先返回流程层的错误，否则返回最近执行组件的 lastError
DetectionError CpuDetectionFlow::lastError() const {
	if (m_lastError.has_error())
		return m_lastError;
	// 按执行顺序反查错误：后处理 -> 推理 -> 预处理
	if (m_postprocessor.lastError().has_error())
		return m_postprocessor.lastError();
	if (m_detector.lastError().has_error())
		return m_detector.lastError();
	return m_preprocessor.lastError();
}
// 构造/配置期产生的警告
const QVector<DetectorWarning>& CpuDetectionFlow::warnings() const {
	return m_detector.warnings();
}

// 预热，执行若干次推理以加载模型和初始化资源
bool CpuDetectionFlow::warmup( int iterations ) {
	// 调用检测器的预热(内部用假输入跑 Run)，失败时产生警告由warnings获取
	return m_detector.warmup( iterations );
}

// 执行完整的处理流程：预处理 + 推理 + 后处理
bool CpuDetectionFlow::run( const FrameHandlePtr& handle, DetectionResult& outResult ) {
	// 复位输出与流程层错误
	outResult = DetectionResult();
	m_lastError = DetectionError();
	// 检查当前的实际生效的模式是否为CPU
	if (m_activeMode != FlowMode::CPU) {
		m_lastError = DetectionError( QStringLiteral( "[CpuDetectionFlow]: 当前流程的实际生效模式非CPU！" ), DetectionErrorCategory::Config, DetectionErrorCode::FallbackRequested );
		outResult.error = m_lastError;
		return false;
	}
	if (!handle) { // 检查帧句柄
		m_lastError = DetectionError( QStringLiteral( "[CpuDetectionFlow]: 帧句柄为空！" ), DetectionErrorCategory::Preprocess, DetectionErrorCode::PreprocessFailed );
		outResult.error = m_lastError;
		return false;
	}
	// 1. 将帧句柄交给预处理进行预处理
	auto preprocessed = m_preprocessor.preprocess( handle, m_detector.inputWidth(), m_detector.inputHeight() );
	if (!preprocessed.has_value()) {
		m_lastError = m_preprocessor.lastError();
		if (!m_lastError.has_error()) { // 兜底错误
			m_lastError = DetectionError( QStringLiteral( "[CpuDetectionFlow]: 预处理失败" ), DetectionErrorCategory::Preprocess, DetectionErrorCode::PreprocessFailed );
		}
		outResult.error = m_lastError;
		return false;
	}
	// 2. 将预处理结果交给CPU推理器进行纯推理(计时，日志在流程层输出)
	std::vector<float> output_data;
	std::vector<int64_t> output_shape;
	QElapsedTimer timer; // 开始计时
	timer.start();
	if (!m_detector.infer( preprocessed.value(), output_data, output_shape )) {
		m_lastError = m_detector.lastError();
		if (!m_lastError.has_error()) { // 兜底错误
			m_lastError = DetectionError( QStringLiteral( "[CpuDetectionFlow]: 推理失败" ), DetectionErrorCategory::Detection, DetectionErrorCode::DetectionFailed );
		}
		outResult.error = m_lastError;
		return false;
	}
	const quint64 duration = timer.elapsed(); // 获取推理耗时(ms)
	// 3. 由输出 shape 反推解析维度：输出布局为 [1, 4+num_classes, num_anchors]
	if (output_shape.size() < 3) {
		m_lastError = DetectionError( QStringLiteral( "[CpuDetectionFlow]: 输出张量维度异常，期望 3 维，实际 " )
			+ QString::number( output_shape.size() ), DetectionErrorCategory::Detection, DetectionErrorCode::DetectionFailed );
		outResult.error = m_lastError;
		return false;
	}
	const int num_classes = static_cast<int>(output_shape[1]) - 4;
	const int num_anchors = static_cast<int>(output_shape[2]);
	if (num_classes <= 0 || num_anchors <= 0) {
		m_lastError = DetectionError( QStringLiteral( "[CpuDetectionFlow]: 输出张量形状异常: [" )
			+ QString::number( output_shape[1] ) + QStringLiteral( ", " ) + QString::number( output_shape[2] )
			+ QStringLiteral( "]，无法解析类别数与锚点数" ), DetectionErrorCategory::Detection, DetectionErrorCode::DetectionFailed );
		outResult.error = m_lastError;
		return false;
	}
	// 4. 将推理输出交给通用后处理器：置信度筛选 + NMS + 映射回原图
	std::vector<Box> boxes;
	if (!m_postprocessor.postProcess( output_data.data(), num_classes, num_anchors,
		preprocessed->letterboxInfo, m_config.conf_threshold, m_config.iou_threshold, boxes )) {
		m_lastError = m_postprocessor.lastError();
		if (!m_lastError.has_error()) { // 兜底错误
			m_lastError = DetectionError( QStringLiteral( "[CpuDetectionFlow]: 后处理失败" ), DetectionErrorCategory::Postprocess, DetectionErrorCode::PostprocessFailed );
		}
		outResult.error = m_lastError;
		return false;
	}
	// 5. 组装结果
	outResult.boxes = QVector<Box>( boxes.begin(), boxes.end() );
	outResult.duration = duration;
	// 逐帧推理日志：按 setInfoInterval 的间隔输出
	Logger::instance()->infoWithInterval( QStringLiteral( "[CpuDetectionFlow]: 检测到%1个目标，推理耗时: %2ms" )
		.arg( outResult.boxes.size() ).arg( duration ) );
	return true;
}
