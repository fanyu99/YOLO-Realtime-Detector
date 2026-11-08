#pragma once
#include "DetectionFlowFactory.h"
#include "FrameSourceFactory.h"
#include "DetectionTypes.h"

// 会话，统一管理 FrameSource 和 DetectionFlow 的生命周期以及配置和配对
// 由Worker统一管理Session生命周期，Manager管理Session状态
// 一个Session对应一个Job，Session的配置由Manager传入，Session的错误由Worker上传到Manager
// 触发fallback后Worker直接释放Session并重新创建
// 错误通过返回值 + lastError()/fallbackRequested() 查询
class DetectionSession final
{
public:
	explicit DetectionSession(const DetectorConfig& config);
	~DetectionSession();
	// 创建帧源（根据当前的配置中的FlowMode创建对应内存类型句柄（GPU/CPU）的帧源(根据帧源类型，image/video等)）
	FrameSourcePtr createFrameSource(FrameSourceType sourceType);
	// 创建文件帧源（图片/视频），memoryType 决定帧句柄内存类型
	FrameSourcePtr createFileFrameSource(QStringView filePath, MemoryType memoryType = MemoryType::CPU);
	// 创建摄像头帧源(ffmpeg)
	FrameSourcePtr createCameraFrameSource(const QString& nameOrId, const QString& formatName = QString(), MemoryType memoryType = MemoryType::CPU);
	// 创建Hik相机帧源（未注册Hik时返回nullptr）
	FrameSourcePtr createHikCameraFrameSource(QStringView serial = {}, CameraTriggerMode mode = CameraTriggerMode::Off, MemoryType memoryType = MemoryType::CPU);
	// 执行推理流程（调用Flow的run）
	// 返回 false 时通过 lastError() 取错误原因；
	// Worker和manager需要在false时判断是否触发fallback(fallbackRequested)
	bool runFlow(const FrameHandlePtr& frame, DetectionResult& outResult);

	// 会话是否构造成功（构造失败时用 lastError() 取原因）
	bool isValid() const { return m_detectionFlow && m_detectionFlow->isValid(); }
	// 最近一次错误（构造失败或 runFlow 失败时有效）
	DetectionError lastError() const { return m_lastError; }
	// 最近一次失败是否触发了 fallback（GPU 不支持，需回退 CPU 重建）
	bool fallbackRequested() const { return m_fallbackRequested; }
	// 获取当前的实际执行模式
	FlowMode currentMode() const { return m_mode; }
	// 构造/预热期产生的警告(如输入尺寸对齐)
	const QVector<DetectorWarning>& warnings() const { return m_warnings; }

private:
	void warmup();	// 预热推理流程（构造函数内自动调用，内部记录警告日志）

	DetectorConfig m_config; // 配置
	DetectionFlowPtr m_detectionFlow; // 推理流程
	FlowMode m_mode; // 当前的真实执行模式：CPU/GPU
	bool m_fallbackRequested = false;	// 是否为 fallback
	DetectionError m_lastError;			// 最近一次错误
	QVector<DetectorWarning> m_warnings;	// 构造/预热期警告
	// 推理流程和帧源工厂
	DetectionFlowFactoryPtr m_flowFactory;
	FrameSourceFactoryPtr m_sourceFactory;
};
