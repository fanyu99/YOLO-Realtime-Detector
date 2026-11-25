#pragma once
#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <atomic>
#include <mutex>
#include "IPerformanceMonitor.h"
#include "PerformanceTypes.h"

// 性能监测器
class PerformanceMonitor final : public QObject, public IPerformanceMonitor {
	Q_OBJECT
public:
	explicit PerformanceMonitor( QObject* parent = nullptr );
	~PerformanceMonitor() override;

	bool isRunning()const noexcept { return m_isRunning; }
	bool isEnabled() const noexcept override;	// 级别为关闭 = Off
	bool isStageEnabled( PerformanceStage stage ) const noexcept override;	// 阶段是否被当前级别白名单允许
	void recordStage( PerformanceStage stage, quint64 nsElapsed ) noexcept override;
	void recordFrameResult( bool success, quint64 frameTotalNs ) noexcept override;

	// 配置更新：推导新的配置的采集掩码并重置统计窗口
	void setConfig( const PerformanceConfig& config ) noexcept override;

	// 作业上下文
	void setJobId( quint64 jobId ) { m_jobId = jobId; }
	void setWarmupIterations( int iterations ) noexcept override;
	void setIsRunning( bool running )override { m_isRunning = running; }

	// 当前生效的级别
	PerformanceLevel level() const noexcept { return m_level.load( std::memory_order_relaxed ); }

signals:
	// 定期性能快照
	void snapshotReady( const PerformanceSnapshot& snapshot );

private:
	void resetWindow() noexcept;	// 清空窗口和snapshotIndex 归零，需要有锁
	void emitSnapshot(); // 生成快照并发信号

	// 采集设置（setConfig 单线程写）
	std::atomic<unsigned> m_stageMask{ 0 };	// 监控级别允许记录的阶段位掩码
	std::atomic<bool> isMemoryRecord{ false }; // 是否采集内存占用
	std::atomic<PerformanceLevel> m_level{ PerformanceLevel::Basic };	//监控级别

	// 窗口累计数据
	std::mutex m_mutex;// 保护窗口数据
	struct StageRecorder {
		uint64_t sampleCount = 0;
		uint64_t totalNs = 0;
		uint64_t maximumNs = 0;
	};
	// 阶段记录器
	StageRecorder m_stageRecorder[PerformanceStageCount];
	quint64 m_windowStartNs = 0;	// 窗口起始时间ns
	uint64_t m_successfulFrames = 0;
	uint64_t m_failedFrames = 0;
	uint64_t m_narrowEndToEndTotalNs = 0;	// 狭义端到端总耗时
	uint64_t m_narrowEndToEndSamples = 0;	// 狭义端到端样本数
	uint64_t m_broadEndToEndTotalNs = 0;	// 广义端到端总耗时
	uint64_t m_broadEndToEndSamples = 0;	// 广义端到端样本数
	uint64_t m_warmupNs=0; // 预热耗时
	int m_warmupIterations = 0;	// 预热轮数
	int m_snapshotIndex = 0;	 // 快照序号

	// 计时器
	QElapsedTimer m_clock;	// 记录窗口时间
	QTimer m_reportTimer;	// 性能快照上报定时器,控制上报时间
	int m_reportIntervalMs = TimeBases::DefaultReportIntervalMs;

	PerformanceConfig m_config;	// 配置副本

	// 运行上下文
	quint64 m_jobId = 0;	// 当前作业 ID
	bool m_isRunning = false;	// 是否有作业在运行，有才进行快照统计和发送信号
};
