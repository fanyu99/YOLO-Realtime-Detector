#pragma once
#include <memory>
#include <QObject>
#include <QtQmlIntegration/QtQmlIntegration>
#include <QString>
#include <QTimer>
#include "IPerformanceMonitor.h"
#include "PerformanceTypes.h"
#include "AppError.h"
#include "Logger.h"
#include "PluginManager.h"
#include "MemoryMonitorPluginInterface.h"
using MonitorPtr = std::unique_ptr<IPerformanceMonitor>;
// 性能app，管理性能监控器的生命周期，快照属性由QML绑定
// 错误使用banner进行提醒
class PerformanceApp : public QObject {
	Q_OBJECT
		QML_ELEMENT
		QML_UNCREATABLE ( "不能够被QML创建实例，属于C++后端单例" )
		Q_PROPERTY ( PerformanceConfig config READ config WRITE setConfig NOTIFY configChanged )

		// QML绑定属性
		Q_PROPERTY ( PerformanceSnapshot snapshot READ snapshot NOTIFY statsChanged )				// 最近一个统计窗口的快照
		Q_PROPERTY ( PerformanceSnapshot overallSnapshot READ overallSnapshot NOTIFY overallSnapshotChanged )	// 本次运行累计的汇总快照

		// 运行上下文
		Q_PROPERTY ( bool isRunning READ isRunning NOTIFY contextChanged )
		Q_PROPERTY ( quint64 jobId READ jobId NOTIFY contextChanged )							// 当前作业ID(0=空闲)
		Q_PROPERTY ( QString preferredModeText READ preferredModeText NOTIFY contextChanged )	// 用户选择的模式
		Q_PROPERTY ( QString activeModeText READ activeModeText NOTIFY contextChanged )		// 实际生效的模式

		// 配置
		Q_PROPERTY ( int levelIndex READ levelIndex WRITE setLevelIndex NOTIFY configChanged )	// 监控级别(PerformanceLevel 枚举值)
		Q_PROPERTY ( int reportInterval READ reportInterval WRITE setReportInterval NOTIFY configChanged )	// 上报间隔(ms)

		// 横幅显示信息
		Q_PROPERTY ( QString bannerText READ bannerText NOTIFY bannerChanged )
		Q_PROPERTY ( BannerLevel bannerLevel READ bannerLevel NOTIFY bannerChanged )

		// 内存快照
		Q_PROPERTY ( MemorySnapshot memorySnapshot READ memorySnapshot NOTIFY memorySnapshotChanged )	// 最近一次内存采样
		Q_PROPERTY ( MemoryOverallStatistics overallMemorySnapshot READ overallMemorySnapshot NOTIFY overallSnapshotChanged )	// 本次运行累计的内存汇总
		Q_PROPERTY ( bool memoryMonitorAvailable READ memoryMonitorAvailable CONSTANT )	// 内存监控插件是否加载

public:
	using PerfAppPtr = std::unique_ptr<PerformanceApp>;
	// 横幅级别
	enum BannerLevel {
		BannerNone = 0,
		BannerSuccess,
		BannerWarning,
		BannerError
	};
	Q_ENUM ( BannerLevel )
		static PerformanceApp* initialize ( const PerformanceConfig& config );// 初始化，需要在instance获取之前，不然会使用默认配置忽略后续的配置
	static PerformanceApp* instance ();
	PerformanceApp ( const PerformanceApp& other ) = delete;
	PerformanceApp& operator = ( const PerformanceApp& other ) = delete;
	~PerformanceApp () override = default;

	Q_INVOKABLE PerformanceConfig config () const;
	Q_INVOKABLE void setConfig ( const PerformanceConfig& config );
	Q_INVOKABLE void dismissBanner () {
		if (m_bannerText.isEmpty ())
			return;
		m_bannerText.clear ();
		m_bannerLevel = BannerNone;
		emit bannerChanged ();
	}

	// 借用监测器
	IPerformanceMonitor* monitor ();

	// 快照属性 getter
	PerformanceSnapshot snapshot () const { return m_snapshot; }
	PerformanceSnapshot overallSnapshot () const { return m_overallSnapshot; }

	// 运行上下文 getter
	bool isRunning () const { return m_isRunning; }
	quint64 jobId () const { return m_jobId; }
	QString preferredModeText () const { return m_preferredModeText; }
	QString activeModeText () const { return m_activeModeText; }

	// 配置 getter / setter(QML 写入口)
	int levelIndex () const { return static_cast<int>(m_config.m_level); }
	void setLevelIndex ( int level );
	int reportInterval () const { return m_config.m_reportInterval; }
	void setReportInterval ( int interval );

	// 运行上下文

	void setIsRunning ( bool running );
	void setCurrentJobId ( quint64 jobId ) {
		if (m_jobId != jobId) { m_jobId = jobId; emit contextChanged (); }
	}
	void setFlowModeTexts ( QStringView preferred, QStringView active ) {
		const QString p = preferred.toString (), a = active.toString ();
		if (m_preferredModeText != p || m_activeModeText != a) {
			m_preferredModeText = p;
			m_activeModeText = a;
			emit contextChanged ();
		}
	}

	// 横幅 getter
	QString bannerText () const { return m_bannerText; }
	BannerLevel bannerLevel () const { return m_bannerLevel; }

	// 内存快照getter
	MemorySnapshot memorySnapshot () const { return m_memorySnapshot; }
	MemoryOverallStatistics overallMemorySnapshot () const { return m_overallMemorySnapshot; }
	bool memoryMonitorAvailable () const { return m_memoryMonitor != nullptr; }

signals:
	void configChanged ();
	void statsChanged ();	// 属性刷新
	void overallSnapshotChanged ();	// 汇总快照刷新,停止/开始触发
	void contextChanged ();	// 运行上下文刷新(jobId / 模式)
	void bannerChanged ();	// 横幅刷新
	void errorOccurred ( const AppError& error );	// 性能监控错误进行提醒
	void memorySnapshotChanged ();	// 内存监控快照提醒

public slots:
	// 接收监测器快照，通知 QML
	void onSnapshotReady ( const PerformanceSnapshot& snapshot );
	// 发生错误，横幅提醒和日志记录
	void onErrorOccurred ( const AppError& error );
	// 运行完成，计算汇总快照数据
	void onRunningChanged ();

private:
	inline static PerfAppPtr m_instance;
	explicit PerformanceApp ( const PerformanceConfig& config, QObject* parent = nullptr );
	void showBanner ( QStringView text, BannerLevel level );	// 统一的横幅出口

	MonitorPtr m_monitor{ nullptr };	// 性能监控器
	PerformanceConfig m_config;	// 性能监控配置

	// 借用插件指针
	MemoryMonitorPluginInterface* m_memoryMonitor = nullptr;
	MemorySnapshot m_memorySnapshot;	// 最近一次内存采样
	MemoryOverallStatistics m_overallMemorySnapshot;	// 本次运行累计的内存汇总
	bool m_memoryErrorReported = false;	// 快照生成失败提示一次

	// 快照数据
	PerformanceSnapshot m_snapshot;			// 最近一个统计窗口的快照
	PerformanceSnapshot m_overallSnapshot;	// 本次运行累计的汇总快照
	int m_snapshotCount = 0;					// 已汇总的快照数

	// 运行上下文
	bool m_isRunning = false; // 是否有作业运行，有才进行快照统计和发送信号
	quint64 m_jobId = 0;					// 当前作业ID(0=空闲)
	QString m_preferredModeText = "CPU";	// 用户选择的模式
	QString m_activeModeText = "CPU";		// 实际生效的模式

	// 横幅
	QString m_bannerText;	// 最近一条横幅文本
	BannerLevel m_bannerLevel = BannerNone;
};