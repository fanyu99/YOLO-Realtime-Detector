#include "PerformanceMonitor.h"
#include "TimeBases.h"

PerformanceMonitor::PerformanceMonitor( QObject* parent )
	: QObject( parent )
{
	m_clock.start();  // 窗口开始
	m_reportTimer.setInterval( m_reportIntervalMs );
	connect( &m_reportTimer, &QTimer::timeout, this, &PerformanceMonitor::emitSnapshot );
	m_reportTimer.start();
}

PerformanceMonitor::~PerformanceMonitor() = default;

bool PerformanceMonitor::isEnabled() const noexcept {
	return m_level.load( std::memory_order_relaxed ) != PerformanceLevel::Off;
}

// 阶段位掩码判断该阶段是否允许记录
bool PerformanceMonitor::isStageEnabled( PerformanceStage stage ) const noexcept {
	const unsigned mask = m_stageMask.load( std::memory_order_relaxed );
	return ( mask & ( 1u << static_cast<unsigned>( stage ) ) ) != 0;
}

// 设置配置并重置窗口
void PerformanceMonitor::setConfig( const PerformanceConfig& config ) noexcept {
	m_config = config;

	// 由白名单表推导采集设置
	const CollectorSettings settings = calcCollectSettings( config.m_level );
	m_stageMask.store( settings.stageMask, std::memory_order_relaxed );
	isMemoryRecord.store( settings.enableMemoryRecord, std::memory_order_relaxed );
	m_level.store( config.m_level, std::memory_order_relaxed );

	// 级别变化重置窗口（与工作线程的 recordStage 互斥）
	{
		std::lock_guard<std::mutex> lock( m_mutex );
		resetWindow();
		m_snapshotIndex = 0;
	}

	// 重设定时器
	if (config.m_reportInterval != m_reportIntervalMs) {
		m_reportIntervalMs = config.m_reportInterval;
		m_reportTimer.setInterval( m_reportIntervalMs );
	}
}

// 过滤不启用的阶段，记录阶段信息
void PerformanceMonitor::recordStage( PerformanceStage stage, quint64 nsElapsed ) noexcept {
	// 掩码过滤阶段
	const unsigned mask = m_stageMask.load( std::memory_order_relaxed );
	const unsigned stageBit = 1u << static_cast<unsigned>( stage );
	// 如果不允许记录，过滤退出
	if ( ( mask & stageBit ) == 0 )
		return;

	// 记录当前的阶段信息
	std::lock_guard<std::mutex> lock( m_mutex );  // 加锁防止脏读
	auto& acc = m_stageRecorder[ static_cast<size_t>( stage ) ];
	++acc.sampleCount;
	acc.totalNs += nsElapsed;
	acc.maximumNs = std::max (nsElapsed,acc.maximumNs);

	// 端到端记录
	if ( stage == PerformanceStage::end2End ) {
		m_narrowEndToEndTotalNs += nsElapsed;
		++m_narrowEndToEndSamples;
	}
	// 预热记录
	if (stage == PerformanceStage::Warmup)
	{
		m_warmupNs = nsElapsed;
	}
}

// 记录本次会话的预热轮数
void PerformanceMonitor::setWarmupIterations( int iterations ) noexcept {
	std::lock_guard<std::mutex> lock( m_mutex );
	m_warmupIterations = iterations;
}

// 记录一帧的结果；frameTotalNs 为该帧从读帧开始的累计耗时(广义端到端)
void PerformanceMonitor::recordFrameResult( bool success, quint64 frameTotalNs ) noexcept {
	std::lock_guard<std::mutex> lock( m_mutex );
	if ( success )
		++m_successfulFrames;
	else
		++m_failedFrames;
	m_broadEndToEndTotalNs += frameTotalNs;
	++m_broadEndToEndSamples;
}

// 重置窗口并刷新起始时间
void PerformanceMonitor::resetWindow() noexcept {
	for ( auto& acc : m_stageRecorder ) {
		acc.sampleCount = 0;
		acc.totalNs = 0;
		acc.maximumNs = 0;
	}
	m_successfulFrames = 0;
	m_failedFrames = 0;
	m_narrowEndToEndTotalNs = 0;
	m_narrowEndToEndSamples = 0;
	m_broadEndToEndTotalNs = 0;
	m_broadEndToEndSamples = 0;
	// m_warmupNs 不重置
	// 重置后记录新窗口的起始时间
	m_windowStartNs = static_cast<quint64>( m_clock.nsecsElapsed() );
}

// 开始时重置统计窗口
// 关闭时发送
void PerformanceMonitor::setIsRunning( bool running ) {
	if (m_isRunning == running)
		return;

	std::lock_guard<std::mutex> lock( m_mutex );
	if (running) {
		resetWindow();
		m_snapshotIndex = 0;
	}
	m_isRunning = running;
}

// flush当前的快照
void PerformanceMonitor::flushSnapshot() {
	emitSnapshot();
}

// 生成快照并发送信号
void PerformanceMonitor::emitSnapshot() {
	if (!m_isRunning)
		return;
	StageRecorder recorderCopy[ std::size( m_stageRecorder ) ];
	uint64_t successfulFrames = 0;
	uint64_t failedFrames = 0;
	uint64_t narrowTotalNs = 0;
	uint64_t narrowSamples = 0;
	uint64_t broadTotalNs = 0;
	uint64_t broadSamples = 0;
	uint64_t warmupNs = 0;
	int warmupIterations = 0;
	quint64 windowStartNs = 0;
	// 用锁拷贝数据到快照，防止一读一写脏数据
	{
		std::lock_guard<std::mutex> lock( m_mutex );
		std::copy( std::begin( m_stageRecorder ), std::end( m_stageRecorder ), std::begin( recorderCopy ) );
		successfulFrames = m_successfulFrames;
		failedFrames = m_failedFrames;
		narrowTotalNs = m_narrowEndToEndTotalNs;
		narrowSamples = m_narrowEndToEndSamples;
		broadTotalNs = m_broadEndToEndTotalNs;
		broadSamples = m_broadEndToEndSamples;
		warmupNs = m_warmupNs;
		warmupIterations = m_warmupIterations;
		windowStartNs = m_windowStartNs;
	}

	// 生成快照并发信号
	PerformanceSnapshot snapshot;
	snapshot.m_context.jobId = m_jobId;
	snapshot.m_context.snapshotIndex = m_snapshotIndex;
	snapshot.m_context.windowDurationMs = static_cast<double>( m_clock.nsecsElapsed() - static_cast<qint64>( windowStartNs ) ) * TimeBases::Ns2Ms;
	snapshot.m_context.level = m_level.load( std::memory_order_relaxed );

	// 整体计数
	const auto recorded = successfulFrames + failedFrames;
	snapshot.m_overall.recordedFrames = static_cast<int>( recorded );
	snapshot.m_overall.successfulFrames = static_cast<int>( successfulFrames );
	snapshot.m_overall.failedFrames = static_cast<int>( failedFrames );
	snapshot.m_overall.warmupNs = warmupNs;
	snapshot.m_overall.warmupIterations = warmupIterations;
	snapshot.m_overall.narrowSamples = narrowSamples;
	snapshot.m_overall.broadSamples = broadSamples;
	// 记录有效窗口的时长以及帧率
	snapshot.m_overall.validWindowDurationMs = recorded > 0
		? snapshot.m_context.windowDurationMs
		: 0.0;
	snapshot.m_overall.overallFPS = snapshot.m_context.windowDurationMs > 0.0
		? static_cast<float>( recorded * TimeBases::Ms2S / snapshot.m_context.windowDurationMs )
		: 0.0f;
	if ( narrowSamples > 0 ) {
		snapshot.m_overall.narrowEndToEndNs = narrowTotalNs / narrowSamples;
	}
	if ( broadSamples > 0 ) {
		snapshot.m_overall.broadEndToEndNs = broadTotalNs / broadSamples;
	}

	// 阶段统计
	const unsigned mask = m_stageMask.load( std::memory_order_relaxed );
	for ( size_t i = 0; i < std::size( recorderCopy ); ++i ) {
		// 跳过未启用的阶段
		if ( ( mask & ( 1u << i ) ) == 0 )
			continue;
		const auto& acc = recorderCopy[ i ];
		// 跳过没有样本的阶段（丢帧除外）
		if ( acc.sampleCount == 0 && static_cast<PerformanceStage>( i ) != PerformanceStage::DisplayDropped )
			continue;
		StageStatistics stat;
		stat.stage = static_cast<PerformanceStage>( i );
		stat.sampleCount = acc.sampleCount;
		stat.averageNs =acc.totalNs ? acc.totalNs / acc.sampleCount : 0;
		stat.maximumNs = acc.maximumNs;
		snapshot.m_stages.append( stat );
	}

	// 序号推进与窗口重启需与 recordStage 互斥
	{
		std::lock_guard<std::mutex> lock( m_mutex );
		++m_snapshotIndex;
		resetWindow();	  // 重启窗口
	}

	emit snapshotReady( snapshot );
}

