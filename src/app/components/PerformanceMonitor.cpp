#include "PerformanceMonitor.h"

namespace {
	// ns -> ms
	constexpr double ns2ms = 1.0e-6;
}

PerformanceMonitor::PerformanceMonitor( QObject* parent )
	: QObject( parent )
{
	m_clock.start();
	m_reportTimer.setInterval( m_reportIntervalMs );
	connect( &m_reportTimer, &QTimer::timeout, this, &PerformanceMonitor::emitSnapshot );
	m_reportTimer.start();
}

PerformanceMonitor::~PerformanceMonitor() = default;

bool PerformanceMonitor::isEnabled() const noexcept {
	return m_level.load( std::memory_order_relaxed ) != PerformanceLevel::Off;
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

	// 记录当前的阶段信息（掩码过滤在锁外完成，被过滤的调用完全无锁）
	const double ms = static_cast<double>( nsElapsed ) * ns2ms;
	std::lock_guard<std::mutex> lock( m_mutex );
	auto& acc = m_stageRecorder[ static_cast<size_t>( stage ) ];
	++acc.sampleCount;
	acc.totalMs += ms;
	if ( ms > acc.maximumMs )
		acc.maximumMs = ms;

	// 端到端单独记录
	if ( stage == PerformanceStage::endToend ) {
		m_endToEndTotalMs += ms;
		++m_endToEndSamples;
	}
}

void PerformanceMonitor::recordFrameResult( bool success ) noexcept {
	std::lock_guard<std::mutex> lock( m_mutex );
	if ( success )
		++m_successfulFrames ;
	else
		++m_failedFrames;
}

// 重置窗口并刷新起始时间
void PerformanceMonitor::resetWindow() noexcept {
	for ( auto& acc : m_stageRecorder ) {
		acc.sampleCount = 0;
		acc.totalMs = 0.0;
		acc.maximumMs = 0.0;
	}
	m_successfulFrames = 0;
	m_failedFrames = 0;
	m_endToEndTotalMs = 0.0;
	m_endToEndSamples = 0;
	// 重置后记录新窗口的起始时间
	m_windowStartNs = static_cast<quint64>( m_clock.nsecsElapsed() );
}

// 生成快照并发送信号
void PerformanceMonitor::emitSnapshot() {
	// 锁内只做拷贝，聚合计算放在锁外，缩短与工作线程的互斥窗口
	StageRecorder recorderCopy[ std::size( m_stageRecorder ) ];
	uint64_t successfulFrames = 0;
	uint64_t failedFrames = 0;
	double endToEndTotalMs = 0.0;
	uint64_t endToEndSamples = 0;
	quint64 windowStartNs = 0;
	{
		std::lock_guard<std::mutex> lock( m_mutex );
		std::copy( std::begin( m_stageRecorder ), std::end( m_stageRecorder ), std::begin( recorderCopy ) );
		successfulFrames = m_successfulFrames;
		failedFrames = m_failedFrames;
		endToEndTotalMs = m_endToEndTotalMs;
		endToEndSamples = m_endToEndSamples;
		windowStartNs = m_windowStartNs;
	}

	// 生成快照并发信号
	PerformanceSnapshot snapshot;
	snapshot.m_context.jobId = m_jobId;
	snapshot.m_context.snapshotIndex = m_snapshotIndex;
	snapshot.m_context.windowDurationMs = static_cast<double>( m_clock.nsecsElapsed() - static_cast<qint64>( windowStartNs ) ) * ns2ms;
	snapshot.m_context.level = m_level.load( std::memory_order_relaxed );

	// 整体计数
	const auto recorded = successfulFrames + failedFrames;
	snapshot.m_overall.recordedFrames = static_cast<int>( recorded );
	snapshot.m_overall.successfulFrames = static_cast<int>( successfulFrames );
	snapshot.m_overall.failedFrames = static_cast<int>( failedFrames );
	snapshot.m_overall.overallFPS = snapshot.m_context.windowDurationMs > 0.0
		? static_cast<float>( recorded * 1000.0 / snapshot.m_context.windowDurationMs )
		: 0.0f;
	if ( endToEndSamples > 0 ) {
		snapshot.m_overall.averageEndToEndMs = endToEndTotalMs / static_cast<double>( endToEndSamples );
	}

	// 阶段统计
	const unsigned mask = m_stageMask.load( std::memory_order_relaxed );
	for ( size_t i = 0; i < std::size( recorderCopy ); ++i ) {
		// 跳过未启用的阶段
		if ( ( mask & ( 1u << i ) ) == 0 )
			continue;
		const auto& acc = recorderCopy[ i ];
		// 跳过没有样本的阶段
		if ( acc.sampleCount == 0 )
			continue;
		StageStatistics stat;
		stat.stage = static_cast<PerformanceStage>( i );
		stat.sampleCount = acc.sampleCount;
		stat.averageMs = acc.totalMs / static_cast<double>( acc.sampleCount );
		stat.maximumMs = acc.maximumMs;
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
