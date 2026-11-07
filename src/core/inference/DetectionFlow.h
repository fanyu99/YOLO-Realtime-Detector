#pragma once
#include "FrameHandle.h"
#include "DetectionTypes.h"
#include <memory>
// 抽象类：处理推理流程的结构
class DetectionFlow {
public:
	DetectionFlow() = default;
	virtual ~DetectionFlow() = default;
	// 执行完整的处理流程：预处理 + 推理 + 后处理
	// 一旦触发fallback(不支持GPU)，销毁当前的流程、重新创建CPU DetectionFlow并执行CPU流程
	virtual bool run( const FrameHandlePtr& handle, DetectionResult& result ) = 0;
	virtual bool warmup( int iterations ) = 0;	// 预热，执行若干次推理以加载模型和初始化资源
	virtual FlowMode activeMode()const = 0;	// 当前流程的实际计算模式：CPU/GPU
	virtual FlowMode preferredMode()const { return FlowMode::CPU; }	// 首选模式：CPU/GPU
	// 检测器会话是否构造成功(构造失败时用 lastError() 取原因)
	virtual bool isValid() const = 0;
	// 最近一次错误(由流程内部各组件上报,调用方主动查询)
	virtual DetectionError lastError() const = 0;
	// 构造/配置期产生的警告(非致命)：如输入尺寸已按模型自动对齐。
	virtual const QVector<DetectorWarning>& warnings() const = 0;
};
using DetectionFlowPtr = std::unique_ptr<DetectionFlow>;
