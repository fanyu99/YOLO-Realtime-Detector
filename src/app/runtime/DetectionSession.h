#pragma once
#include "DetectionFlowFactory.h"
#include "FrameSourceFactory.h"
#include "DetectionTypes.h"

// 会话，统一管理 FrameSource 和 DetectionFlow 的生命周期以及配置和配对
// 由Worker统一管理Session生命周期，Manager管理Session状态
// 一个Session对应一个Job，Session的配置由Manager传入，Session的错误由Worker上传到Manager
// 触发fallback后Worker直接释放Session并重新创建
class DetectionSession	final: public QObject
{
	Q_OBJECT


public:
	explicit DetectionSession(const DetectorConfig& config,QObject* parent = nullptr);
	~DetectionSession();
	// 创建帧源（根据当前的配置中的FlowMode创建对应内存类型句柄（GPU/CPU）的帧源(根据帧源类型，image/video等)）
	FrameSourcePtr createFrameSource(FrameSourceType sourceType);
	// 创建文件帧源（图片/视频），memoryType 决定帧句柄内存类型
	FrameSourcePtr createFileFrameSource(QStringView filePath, MemoryType memoryType = MemoryType::CPU);
	// 创建摄像头帧源(ffmpeg)
	FrameSourcePtr createCameraFrameSource(const QString& nameOrId, const QString& formatName = QString(), MemoryType memoryType = MemoryType::CPU);
	// 创建Hik相机帧源（未注册Hik时返回nullptr）
	FrameSourcePtr createHikCameraFrameSource(QStringView serial = {}, CameraTriggerMode mode = CameraTriggerMode::Off, MemoryType memoryType = MemoryType::CPU);
	// 执行推理流程（调用Flow的run,如果执行失败且GPU不支持，发送fallback信号）
	// 错误发送errorOccurred信号
	bool runFlow(const FrameHandlePtr& frame,DetectionResult& outResult);


	FlowMode currentMode()const;
	
signals:
	void fallbackToCPU(const DetectionError& error);	// GPU不支持，回退到CPU模式
	void errorOccurred( const DetectionError& error );	// 执行错误


private:
	DetectorConfig m_config; // 配置
	DetectionFlowPtr m_detectionFlow; // 推理流程
	FlowMode m_mode; // 当前的真实执行模式：CPU/GPU
	// 推理流程和帧源工厂
	DetectionFlowFactoryPtr m_flowFactory;
	FrameSourceFactoryPtr m_sourceFactory;
};

