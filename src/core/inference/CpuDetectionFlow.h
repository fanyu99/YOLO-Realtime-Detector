#pragma once
#include "DetectionFlow.h"
#include "CpuDetector.h"
#include "CpuPreprocessor.h"
#include "CommonPostProcessor.h"

// 统一的CPU完整处理流程：预处理 + 推理 + 后处理
// 处理流程中出现错误时，使用result的error回填错误信息
class CpuDetectionFlow final: public DetectionFlow {
public:
	CpuDetectionFlow() = default;
	explicit CpuDetectionFlow( const DetectorConfig& config );
	~CpuDetectionFlow() override = default;
	// 执行完整的CPU处理流程：预处理 + 推理  出参为outResult
	// 成功返回true,失败返回false，并在result中回填错误信息
	bool run( const FrameHandlePtr& handle, DetectionResult& outResult ) override;
	// 预热，执行若干次推理以加载模型和初始化资源
	bool warmup( int iterations ) override;
	// 当前流程的计算模式：CPU
	FlowMode activeMode()const override;
	// 首选的模式：CPU
	FlowMode preferredMode()const override;

	// 检测器会话是否构造成功(构造失败时用 lastError() 取原因)
	bool isValid() const override;
	// 最近一次错误(由流程内部各组件上报,调用方主动查询)
	DetectionError lastError() const override;
	// 构造/配置期产生的警告(非致命)：如输入尺寸已按模型自动对齐。
	const QVector<DetectorWarning>& warnings() const override;

private:
	CpuDetector m_detector; // 推理器
	CpuPreprocessor m_preprocessor;	// CPU 预处理器
	CommonPostProcessor m_postprocessor; // 通用后处理器
	DetectorConfig m_config;	// 配置副本(供流程校验与阈值传递)
	DetectionError m_lastError;	// 流程层最近一次错误
	FlowMode m_activeMode = FlowMode::CPU;	// 当前流程的实际的计算模式：CPU
	FlowMode m_preferredMode = FlowMode::CPU;	// 首选模式：CPU
};
