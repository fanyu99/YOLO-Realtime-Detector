#pragma once
#include <QString>
#include <QVector>
#include <QObject>
#include "DetectionTypes.h"
#include "TimeBases.h"

// 检测的当前的状态阶段
enum class PerformanceStage {
	ReadFrame,	   // 读取帧
	Frame2Gpu,		  // 上传到GPU
	Frame2Cpu,  // 下载到CPU(拷贝)
	Preprocessing,		// 预处理
	Inference, // 推理
	Postprocessing,	 // 后处理
	RenderImage,					// 渲染图片
	ToQImage,						// 烧框结果转 QImage(离线显示)
	DisplayPrepare,		// 显示数据准备：推理完成-快照打包并投递进邮箱
	DisplayWait,				// 显示等待：快照入邮箱-Overlay取走快照渲染
	PresentLatency,		// 提交呈现延迟：读帧开始-帧进入窗口显示
	DisplayDropped,		// 丢弃帧数：sampleCount为丢弃数
	Save, // 保存图像
	end2End,		 // 端到端总耗时
	Count		 // 阶段总数
};

// 阶段总数
inline constexpr size_t PerformanceStageCount = static_cast<size_t>( PerformanceStage::Count );

// 将阶段枚举转换为字符串
inline QString stageToString( PerformanceStage stage ) {
	switch (stage) {
	case PerformanceStage::ReadFrame: return QStringLiteral( "读帧(ReadFrame)" );
	case PerformanceStage::Frame2Gpu: return QStringLiteral( "上传(Upload)" );
	case PerformanceStage::Frame2Cpu: return QStringLiteral ( "下载(Download)" );
	case PerformanceStage::Preprocessing: return QStringLiteral( "预处理(Preprocessing)" );
	case PerformanceStage::Inference: return QStringLiteral( "推理(Inference)" );
	case PerformanceStage::Postprocessing: return QStringLiteral( "后处理(Postprocessing)" );
	case PerformanceStage::RenderImage: return QStringLiteral( "渲染图像(RenderImage)" );
	case PerformanceStage::ToQImage: return QStringLiteral( "转换QImage(ToQImage)" );
	case PerformanceStage::DisplayPrepare: return QStringLiteral( "显示准备(DisplayPrepare)" );
	case PerformanceStage::DisplayWait: return QStringLiteral( "显示等待(DisplayWait)" );
	case PerformanceStage::PresentLatency: return QStringLiteral( "呈现延迟(PresentLatency)" );
	case PerformanceStage::DisplayDropped: return QStringLiteral( "丢弃帧(DisplayDropped)" );
	case PerformanceStage::Save: return QStringLiteral( "保存(Save)" );
	case PerformanceStage::end2End: return QStringLiteral( "端到端(end2End)" );
	default:
		return QStringLiteral( "未知(Unknown)" );
	}
}

 // 性能监控配置
// 性能级别、性能快照上报间隔
struct 	PerformanceConfig {
	Q_GADGET
public:
	// 性能监控级别
	enum class PerformanceLevel {
		Off = 0,	// 关闭，不进行记录
		Minimum,	// 仅端到端耗时与整体FPS
		Basic,		// 最小+ 推理链路全阶段(读帧/下载/预处理/推理/后处理)
		Detailed,	// Basic+显示/保存链路(渲染/转QImage/保存/显示等待/丢弃帧) + 内存占用
		Full		// Detailed + GPU侧阶段(上传/显示准备/呈现延迟)
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
	int m_reportInterval = TimeBases::DefaultReportIntervalMs;	// 上报间隔，单位毫秒
};
using PerformanceLevel = PerformanceConfig::PerformanceLevel;

// 由级别推导出的采集设置
struct CollectorSettings {
	unsigned stageMask = 0;		// 阶段位掩码
	bool enableMemoryRecord = false;	// 是否启用内存占用采样
	bool recordGpuStages = false;	// 是否跟踪 GPU 侧的性能如Upload
};

// 由级别推导出采集设置
inline CollectorSettings calcCollectSettings(PerformanceLevel level) noexcept {
	CollectorSettings setting;
	unsigned mask = (1u << static_cast<unsigned>(PerformanceStage::end2End));	 // 端到端皆有
	switch (level) {
	case PerformanceLevel::Off:
		mask = 0;	// 关闭
		break;
	case PerformanceLevel::Minimum:
		break;
		// Basic：推理流程
	case PerformanceLevel::Basic:
		mask |= (1u << static_cast<unsigned>(PerformanceStage::ReadFrame))
			| (1u << static_cast<unsigned>(PerformanceStage::Frame2Cpu))
			| (1u << static_cast<unsigned>(PerformanceStage::Preprocessing))
			| (1u << static_cast<unsigned>(PerformanceStage::Inference))
			| (1u << static_cast<unsigned>(PerformanceStage::Postprocessing));
		break;
		// Detailed：+ 显示/保存链路 + 内存占用
	case PerformanceLevel::Detailed:
		mask |= (1u << static_cast<unsigned>(PerformanceStage::ReadFrame))
			| (1u << static_cast<unsigned>(PerformanceStage::Frame2Cpu))
			| (1u << static_cast<unsigned>(PerformanceStage::Preprocessing))
			| (1u << static_cast<unsigned>(PerformanceStage::Inference))
			| (1u << static_cast<unsigned>(PerformanceStage::Postprocessing))
			| (1u << static_cast<unsigned>(PerformanceStage::RenderImage))
			| (1u << static_cast<unsigned>(PerformanceStage::ToQImage))
			| (1u << static_cast<unsigned>(PerformanceStage::Save))
			| (1u << static_cast<unsigned>(PerformanceStage::DisplayWait))
			| (1u << static_cast<unsigned>(PerformanceStage::DisplayDropped));
		setting.enableMemoryRecord = true;
		break;
		// Full：+ GPU 侧的计时
	case PerformanceLevel::Full:
		mask |= (1u << static_cast<unsigned>(PerformanceStage::ReadFrame))
			| (1u << static_cast<unsigned>(PerformanceStage::Frame2Gpu))
			| (1u << static_cast<unsigned>(PerformanceStage::Frame2Cpu))
			| (1u << static_cast<unsigned>(PerformanceStage::Preprocessing))
			| (1u << static_cast<unsigned>(PerformanceStage::Inference))
			| (1u << static_cast<unsigned>(PerformanceStage::Postprocessing))
			| (1u << static_cast<unsigned>(PerformanceStage::RenderImage))
			| (1u << static_cast<unsigned>(PerformanceStage::ToQImage))
			| (1u << static_cast<unsigned>(PerformanceStage::Save))
			| (1u << static_cast<unsigned>(PerformanceStage::DisplayWait))
			| (1u << static_cast<unsigned>(PerformanceStage::DisplayDropped))
			| (1u << static_cast<unsigned>(PerformanceStage::DisplayPrepare))
			| (1u << static_cast<unsigned>(PerformanceStage::PresentLatency));
		setting.enableMemoryRecord = true;
		setting.recordGpuStages = true;
		break;
	}
	setting.stageMask = mask;
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
// 统一使用ns作为时间单位
struct StageStatistics {
	PerformanceStage stage = PerformanceStage::end2End; // 阶段
	uint64_t sampleCount = 0; // 样本数
	uint64_t averageNs = 0;	// 平均耗时
	uint64_t maximumNs = 0;		// 最大耗时
};

// 整体计数
struct OverallStatistics{
	float overallFPS = 0.0f;	// 处理帧率(等于成功处理的帧数除以统计窗口时长)
	uint64_t narrowEndToEndNs = 0;	// 从读帧到推理完成的平均耗时
	uint64_t broadEndToEndNs = 0;	// // 从读帧到一帧完成显示的平均耗时
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
