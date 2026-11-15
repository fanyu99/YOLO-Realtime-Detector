#pragma once
#include <QElapsedTimer>
#include <QString>

struct PerformanceConfig;

// 检测的当前的状态阶段
enum class PerformanceStage {
	ReadFrame,	   // 读取帧
	Upload,		  // 上传到GPU
	Preprocessing,		// 预处理
	Inference, // 推理
	Postprocessing,	 // 后处理
	RenderImage,					// 渲染图片
	Save, // 保存图像
	endToend		 // 端到端总耗时
};


// 性能监控接口
// isEnabled / recordStage / recordFrameResult	随便调用
// 配置更新、snapshotReady信号 仅在监测器所属线程调用
class IPerformanceMonitor {
public:
	virtual ~IPerformanceMonitor() = default;
	virtual bool isEnabled() const noexcept= 0;	  // 返回是否全局启用性能监控器
	virtual void recordStage( PerformanceStage stage,quint64 nsElapsed)noexcept  = 0;		  // 记录阶段耗时，单位为ns
	virtual void recordFrameResult( bool success ) noexcept = 0;	  // 记录一帧是否成功
	virtual void setConfig(const PerformanceConfig&config) noexcept = 0;
};

// 将阶段枚举转换为字符串
inline QString stageToString( PerformanceStage stage ) {
	switch (stage) {
	case PerformanceStage::ReadFrame: return QStringLiteral( "读帧(ReadFrame)" );
	case PerformanceStage::Upload: return QStringLiteral( "上传(Upload)" );
	case PerformanceStage::Preprocessing: return QStringLiteral( "预处理(Preprocessing)" );
	case PerformanceStage::Inference: return QStringLiteral( "推理(Inference)" );
	case PerformanceStage::Postprocessing: return QStringLiteral( "后处理(Postprocessing)" );
	case PerformanceStage::RenderImage: return QStringLiteral( "渲染图像(RenderImage)" );
	case PerformanceStage::Save: return QStringLiteral( "保存(Save)" );
	case PerformanceStage::endToend: return QStringLiteral( "端到端(endToend)" );
	default:
		return QStringLiteral( "未知(Unknown)" );
	}
}