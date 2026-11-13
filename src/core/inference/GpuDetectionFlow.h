#pragma once
#include "DetectionFlow.h"
#ifdef YOLO_HAVE_CUDA
#include  "GpuDetector.h"
#include "CudaPreprocessor.h"
#endif
#include "DetectionTypes.h"
#include "CommonPostProcessor.h"
#include <QVector>
#include <QString>
// Gpu推理流程
class GpuDetectionFlow : public DetectionFlow {
public:
	GpuDetectionFlow() = default;
	// 创建检测器时如果出现fallback，设置错误fallback
	explicit GpuDetectionFlow( const DetectorConfig& config );
	~GpuDetectionFlow() override = default;

	// 执行完整的GPU处理流程：预处理 + 推理  出参为outResult
	// 成功返回true,失败返回false，并在result中回填错误信息
	bool run( const FrameHandlePtr& handle, DetectionResult& outResult ) override;

	// 预热
	bool warmup( int iterations ) override;

	// 当前的实际模式
	 FlowMode activeMode()const override {
		return m_activeMode;
	}

	// 用户选择的模式
	 FlowMode preferredMode()const override {
		return m_preferredMode;
	}

	// 是否已经初始化成功（Detector）
	 bool isValid() const override;

	// 获取最近的一次错误
	DetectionError lastError() const override;

	// 获取警告
	const QVector<DetectorWarning>& warnings() const override;

private:
	GpuDetector m_detector;
	CudaPreprocessor m_preprocessor;	// GPU预处理器
	CommonPostProcessor m_postprocessor;	// 通用后处理器
	DetectorConfig m_config;	// 配置副本(供流程校验与阈值传递)
	DetectionError m_lastError;
	FlowMode m_activeMode = FlowMode::GPU;	// 当前流程的实际的计算模式：GPU
	FlowMode m_preferredMode = FlowMode::GPU;	// 首选模式：GPU
};