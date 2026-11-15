#include "PerformanceApp.h"
#include "PerformanceMonitor.h"
#include "NullPerformanceMonitor.h"

PerformanceApp::PerformanceApp(const PerformanceConfig& config, QObject* parent )
	: m_config( config )
	,QObject( parent )
{
	// 如果是OFF，直接创建NULL检测器
	if ( config.m_level == PerformanceLevel::Off ) {
		m_monitor = std::make_unique<NullPerformanceMonitor>();
		return;
	}
	// 如果不是OFF，创建真实的检测器
	auto* concrete = new PerformanceMonitor( this );
	m_monitor.reset( concrete );
	connect( concrete, &PerformanceMonitor::snapshotReady,
			 this, &PerformanceApp::onSnapshotReady );
	connect( this, &PerformanceApp::errorOccurred,
			 this, &PerformanceApp::onErrorOccurred );
	m_monitor->setConfig( config );
}


PerformanceConfig PerformanceApp::config() const {
	return m_config;
}

void PerformanceApp::setConfig( const PerformanceConfig& config ) {
	if (config.m_reportInterval <= 0)
	{
		emit errorOccurred( AppError( QStringLiteral("[PerformanceApp]：报告间隔必须大于0"), AppErrorCode::PerformanceMonitorError));
		Logger::instance()->error(QStringLiteral("[PerformanceApp]：报告间隔必须大于0"));
		return;
	}
	// 更改开关时需要进行重启生效
	if ( ( m_config.m_level == PerformanceLevel::Off ) != ( config.m_level == PerformanceLevel::Off ) ) {
		emit errorOccurred( AppError( QStringLiteral("[PerformanceApp]：性能监控启用/关闭状态的变更需重启程序生效"), AppErrorCode::PerformanceMonitorError ) );
		Logger::instance()->warning( QStringLiteral("[PerformanceApp]：性能监控启用/关闭状态的变更需重启程序生效") );
		return;
	}
	m_config = config;
	if (m_monitor)
		m_monitor->setConfig(config);	
	emit configChanged();
}

IPerformanceMonitor* PerformanceApp::monitor()  {
	return m_monitor.get();
}


void PerformanceApp::onSnapshotReady( const PerformanceSnapshot& snapshot ) {
	// 整体计数
	m_overallFPS = snapshot.m_overall.overallFPS;
	m_averageEndToEndMs = snapshot.m_overall.averageEndToEndMs;
	m_recordedFrames = snapshot.m_overall.recordedFrames;
	m_successfulFrames = snapshot.m_overall.successfulFrames;
	m_failedFrames = snapshot.m_overall.failedFrames;

	// 上下文
	m_windowDurationMs = snapshot.m_context.windowDurationMs;
	m_snapshotIndex = snapshot.m_context.snapshotIndex;

	// 阶段统计组装
	m_stageStats.clear();
	m_stageStats.reserve( snapshot.m_stages.size() );
	for (const StageStatistics& stat : snapshot.m_stages) {
		QVariantMap row;
		row[QStringLiteral("name")] = stageToString( stat.stage );
		row[QStringLiteral("averageMs")] = stat.averageMs;
		row[QStringLiteral("maximumMs")] = stat.maximumMs;
		row[QStringLiteral("sampleCount")] = static_cast<qint64>( stat.sampleCount );
		m_stageStats.append( row );
	}

	emit statsChanged();
}

void PerformanceApp::onErrorOccurred( const AppError& error ) {
	// 记录日志并发横幅
	Logger::instance()->warning( error.errorMessage );
	showBanner( error.errorMessage, BannerWarning );
}

void PerformanceApp::showBanner( QStringView text, BannerLevel level ) {
	m_bannerText = text.toString();
	m_bannerLevel = level;
	emit bannerChanged();
}
