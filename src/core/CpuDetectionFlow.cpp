#include "CpuDetectionFlow.h"
#include <QString>

CpuDetectionFlow::CpuDetectionFlow( const DetectorConfig& config )
	: m_detector( config ) {
}

ActiveMode CpuDetectionFlow::activeMode() const {
	return ActiveMode::CPU;
}

// 预热，执行若干次推理以加载模型和初始化资源
bool CpuDetectionFlow::warmup( int iterations ) {
	if (iterations <= 0) {
		return false;
	}
	// 创建一个空的帧句柄用于预热
	auto emptyFrame = std::make_shared<CpuFrameHandle>( cv::Mat() );
	DetectionResult warmupResult;
	for (int i = 0; i < iterations; ++i) {
		if (!run( emptyFrame, warmupResult )) {
			return false;
		}
	}
	return true;
}


// 执行完整的处理流程：预处理 + 推理 + 后处理
bool CpuDetectionFlow::run( const FrameHandlePtr& handle, DetectionResult& outResult ) {
	// 复位输出
	outResult = DetectionResult();
	if (!handle) { // 检查帧句柄
		outResult.error = DetectionError( QStringLiteral( "[CpuDetectionFlow]: 帧句柄为空！" ), DetectionErrorCategory::Preprocess, DetectionErrorCode::PreprocessFailed );
		return false;
	}
	// 1. 将帧句柄交给预处理进行预处理
	auto preprocessed = m_preprocessor.preprocess( handle, m_detector.inputWidth(), m_detector.inputHeight() );
	if (!preprocessed.has_value()) {
		outResult.error = m_preprocessor.lastError();
		if (!outResult.error.has_error()) { // 兜底错误
			outResult.error = DetectionError( QStringLiteral( "[CpuDetectionFlow]: 预处理失败" ), DetectionErrorCategory::Preprocess, DetectionErrorCode::PreprocessFailed );
		}
		return false;
	}
	// 2. 将预处理结果交给CPU推理器进行推理
	
	// 校验最后的推理结果是否存在错误
	if (outResult.error.has_error()) {
		return false;
	}
	else return true;

}

