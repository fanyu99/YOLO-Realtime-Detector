#pragma once
#include <QString>
#include <QVector>
#include <QObject>
#include "DetectionTypes.h"
#include  "IPerformanceMonitor.h"

namespace {
	// 默认的性能监控上报间隔（单位：毫秒）
	constexpr int kDefaultReportInterval = 300;
}

 // 性能监控配置
// 性能级别、性能快照上报间隔
struct 	PerformanceConfig {
	Q_GADGET
public:
	// 性能监控级别
	enum class PerformanceLevel {
		Off = 0,	// 关闭，不进行记录
		Basic ,	// 仅记录端到端耗时以及整体FPS
		Detailed, // 记录Basic+阶段耗时及实时帧率、内存占用等
		Full  // 包含GPU侧的预处理/推理等信息
	};
	Q_ENUM(PerformanceLevel)


	Q_PROPERTY( PerformanceLevel level READ level WRITE setLevel )
	Q_PROPERTY( int reportInterval READ reportInterval WRITE setReportInterval )


	Q_INVOKABLE  PerformanceLevel level() const { return m_level; }
	Q_INVOKABLE void setLevel(PerformanceLevel level) { m_level = level; }
	Q_INVOKABLE int reportInterval() const { return m_reportInterval; }
	Q_INVOKABLE void setReportInterval(int reportInterval) { m_reportInterval = reportInterval; }

	// 成员
	PerformanceLevel m_level = PerformanceLevel::Basic;	// 性能监控级别
	int m_reportInterval = kDefaultReportInterval;	// 上报间隔，单位毫秒
};
using PerformanceLevel = PerformanceConfig::PerformanceLevel;

// 由级别推导出的采集设置
struct CollectorSettings {
	unsigned stageMask = 0;		// 阶段位掩码
	bool enableMemoryRecord = false;	// 是否启用内存占用采样
	bool recordGpuStages = false;	// 是否跟踪 GPU 侧的性能如Upload 
};

// 计算级别 ：采集点的白名单表
inline CollectorSettings calcCollectSettings(PerformanceLevel level) noexcept {
	CollectorSettings setting;
	switch (level) {
	case PerformanceLevel::Off:
		break;	// 关闭
		// Basic 只采集端到端耗时
	case PerformanceLevel::Basic:
		setting.stageMask = (1u << static_cast<unsigned>(PerformanceStage::endToend));
		break;
		// Detailed 采集端到端 + CPU 侧全阶段 + 内存采样
	case PerformanceLevel::Detailed:
		setting.stageMask = (1u << static_cast<unsigned>(PerformanceStage::endToend))
			| (1u << static_cast<unsigned>(PerformanceStage::ReadFrame))
			| (1u << static_cast<unsigned>(PerformanceStage::Preprocessing))
			| (1u << static_cast<unsigned>(PerformanceStage::Inference))
			| (1u << static_cast<unsigned>(PerformanceStage::Postprocessing))
			| (1u << static_cast<unsigned>(PerformanceStage::RenderImage))
			| (1u << static_cast<unsigned>(PerformanceStage::Save));
		setting.enableMemoryRecord = true;
		break;
		// Detailed + GPU 侧阶段（Upload）
	case PerformanceLevel::Full:
		setting.stageMask = (1u << static_cast<unsigned>(PerformanceStage::endToend))
			| (1u << static_cast<unsigned>(PerformanceStage::ReadFrame))
			| (1u << static_cast<unsigned>(PerformanceStage::Upload))
			| (1u << static_cast<unsigned>(PerformanceStage::Preprocessing))
			| (1u << static_cast<unsigned>(PerformanceStage::Inference))
			| (1u << static_cast<unsigned>(PerformanceStage::Postprocessing))
			| (1u << static_cast<unsigned>(PerformanceStage::RenderImage))
			| (1u << static_cast<unsigned>(PerformanceStage::Save));
		setting.enableMemoryRecord = true;
		setting.recordGpuStages = true;
		break;
	}
	return setting;
}

// 上下文信息
struct ContextInfo {
	quint64 jobId = 0; // 作业ID
	FlowMode   activeMode = FlowMode::CPU; // 实际的计算模式
	FlowMode  preferredMode = FlowMode::CPU; // 用户选择的模式
	int snapshotIndex = 0; // 快照序号
	double windowDurationMs = 0.0; // 统计窗口时长，单位毫秒
	PerformanceLevel level = PerformanceLevel::Basic; // 快照的监控级别
};

// 阶段统计信息
struct StageStatistics {
	PerformanceStage stage = PerformanceStage::endToend; // 阶段
	uint64_t sampleCount = 0; // 样本数
	double averageMs = 0.0;	 // 平均耗时
	double maximumMs = 0.0;		// 最大耗时
};

// 整体计数
struct OverallStatistics{
	float overallFPS = 0.0f;	// 处理帧率(等于成功处理的帧数除以统计窗口时长)
	float averageEndToEndMs = 0.0;	// 单帧端到端平均耗时
	int recordedFrames = 0;	// 统计窗口内记录的帧数(成功+失败)
	int successfulFrames = 0;	// 成功处理的帧数
	int failedFrames = 0;		// 失败的帧数
};

// 性能监控快照
struct PerformanceSnapshot {
	ContextInfo m_context;	// 采集配置与作业上下文
	OverallStatistics m_overall;	// 整体计数
	QVector<StageStatistics> m_stages;	// 阶段采集点（监控级别允许的阶段）
};
// 跨线程传递
Q_DECLARE_METATYPE(PerformanceSnapshot)
