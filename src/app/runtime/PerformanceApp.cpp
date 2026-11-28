#include "PerformanceApp.h"
#include "AppConfigWriter.h"
#include "PerformanceMonitor.h"
#include "NullPerformanceMonitor.h"
#include "MemoryMonitorPluginInterface.h"


PerformanceApp* PerformanceApp::initialize ( const PerformanceConfig& config ) {
	if (!m_instance)
	{
		m_instance.reset ( new PerformanceApp ( config ) );
	}
	// 已经初始化，警告
	else {
		m_instance->showBanner ( QStringLiteral ( "[PerformanceApp]: 重复初始化，已忽略配置！" ), BannerLevel::BannerWarning );
	}
	return m_instance.get ();
}

PerformanceApp* PerformanceApp::instance () {
	if (!m_instance)
		Logger::instance ()->fatal ( QStringLiteral ( "[PerformanceApp]: 未初始化，必须先调用 initialize()!" ) );
	return m_instance.get ();
}


PerformanceApp::PerformanceApp ( const PerformanceConfig& config, QObject* parent )
	: m_config ( config )
	, QObject ( parent )
{
	// 如果是OFF，直接创建NULL检测器
	if (config.m_level == PerformanceLevel::Off) {
		m_monitor = std::make_unique<NullPerformanceMonitor> ();
		return;
	}
	// 如果不是OFF，创建真实的检测器
	auto* concrete = new PerformanceMonitor ( this );
	m_monitor.reset ( concrete );
	// 借用内存监控插件（如果未加载，nullptr）
	m_memoryMonitor = PluginManager::instance ()->getMemoryMonitorPlugin (QString::fromLatin1 ( kPerformanceMemoryMonitorPluginId ) );
	connect ( concrete, &PerformanceMonitor::snapshotReady,
		this, &PerformanceApp::onSnapshotReady );
	connect ( this, &PerformanceApp::errorOccurred,
		this, &PerformanceApp::onErrorOccurred );
	m_monitor->setConfig ( config );
}

PerformanceConfig PerformanceApp::config () const {
	return m_config;
}

void PerformanceApp::setConfig ( const PerformanceConfig& config ) {
	if (config.m_reportInterval <= 0)
	{
		emit errorOccurred ( AppError ( QStringLiteral ( "[PerformanceApp]：报告间隔必须大于0" ), AppErrorCode::PerformanceMonitorError ) );
		Logger::instance ()->error ( QStringLiteral ( "[PerformanceApp]：报告间隔必须大于0" ) );
		emit configChanged ();
		return;
	}
	// 防重复修改
	if (m_config.m_level == config.m_level && m_config.m_reportInterval == config.m_reportInterval) {
		emit configChanged ();
		return;
	}
	// 更改开关时需要进行重启生效
	if ((m_config.m_level == PerformanceLevel::Off) != (config.m_level == PerformanceLevel::Off)) {
		Logger::instance ()->warning ( QStringLiteral ( "[PerformanceApp]：性能监控启用/关闭状态的变更需重启程序生效" ) );
	}
	m_config = config;
	if (m_monitor)
		m_monitor->setConfig ( config );
	emit configChanged ();

	// 写入配置文件，失败只进行提醒
	AppConfigWriter* writer = AppConfigWriter::instance ();
	if (!writer->writePerformanceConfig ( config ))
		emit errorOccurred ( AppError ( QStringLiteral ( "[PerformanceApp]: 配置已生效，但写入配置文件失败: " )
			+ writer->errorMessage (), AppErrorCode::ConfigWriteFailed ) );
}

IPerformanceMonitor* PerformanceApp::monitor () {
	return m_monitor.get ();
}

void PerformanceApp::setLevelIndex ( int level ) {
	PerformanceConfig next = m_config;
	next.m_level = static_cast<PerformanceLevel>(level);
	setConfig ( next );
}

void PerformanceApp::setReportInterval ( int interval ) {
	PerformanceConfig next = m_config;
	next.m_reportInterval = interval;
	setConfig ( next );
}

void PerformanceApp::setIsRunning ( bool running ) {
	if (m_isRunning == running)
		return;

	// 如果停止，flush当前快照计入统计
	if (!running)
	{
		m_monitor->flushSnapshot ();
		showBanner ( QStringLiteral ( "汇总完成：" ) + QString::number ( m_snapshotCount ) + QStringLiteral ( "个快照，汇总展示中" ), BannerSuccess );
	}

	m_monitor->setIsRunning ( running );
	m_isRunning = running;
	onRunningChanged ();
	emit contextChanged ();
}


void PerformanceApp::onSnapshotReady ( const PerformanceSnapshot& snapshot ) {
	m_snapshot = snapshot;
	m_overallSnapshot += snapshot;
	++m_snapshotCount;
	const bool memoryEnabled = calcCollectSettings ( m_config.m_level ).enableMemoryRecord;
	if (memoryEnabled && m_memoryMonitor) {
		if (const auto memory = m_memoryMonitor->getMemorySnapshot ()) {
			MemorySnapshot sample = *memory;
			sample.weightMs = m_snapshot.m_context.windowDurationMs > 0.0
				? m_snapshot.m_context.windowDurationMs : 1.0;
			m_memorySnapshot = sample;
			m_overallMemorySnapshot += sample;
			emit memorySnapshotChanged ();
		}
		else if (!m_memoryErrorReported) {
			m_memoryErrorReported = true;
			emit errorOccurred ( AppError ( QStringLiteral ( "[PerformanceApp]: 内存监控插件获取内存快照失败" ), AppErrorCode::PluginError ) );
		}
	}
	emit statsChanged ();
}

void PerformanceApp::onErrorOccurred ( const AppError& error ) {
	// 记录日志并发横幅
	Logger::instance ()->warning ( error.errorMessage );
	showBanner ( error.errorMessage, BannerWarning );
}

void PerformanceApp::showBanner ( QStringView text, BannerLevel level ) {
	m_bannerText = text.toString ();
	m_bannerLevel = level;
	emit bannerChanged ();
}

// 运行状态变化，清空汇总，结束收尾并通知
void PerformanceApp::onRunningChanged () {
	// 如果开始运行，清空之前的快照和汇总快照
	if (m_isRunning) {
		m_snapshot = {};
		m_snapshotCount = 0;
		emit statsChanged ();
		if (m_memoryMonitor)
		{
			m_memorySnapshot = {};
			m_overallMemorySnapshot = {};
			emit memorySnapshotChanged ();
		}
	}
		
	emit overallSnapshotChanged ();
}
