#pragma once
#include <memory>
#include <QObject>
#include <QVariantList>
#include <QtQmlIntegration/QtQmlIntegration>
#include <QString>
#include "IPerformanceMonitor.h"
#include "PerformanceTypes.h"
#include "AppError.h"
#include "Logger.h"
using MonitorPtr = std::unique_ptr<IPerformanceMonitor>;

// TODO： 添加QML前端性能面板并将日志迁移到性能面板中

// 性能app，管理性能监控器的生命周期，快照属性由QML绑定
// 错误使用banner进行提醒
class PerformanceApp : public QObject {
	Q_OBJECT
	QML_ELEMENT
	Q_PROPERTY(PerformanceConfig config READ config WRITE setConfig NOTIFY configChanged)

	// QML绑定属性
	Q_PROPERTY(float overallFPS READ overallFPS NOTIFY statsChanged)					// 整体帧率
	Q_PROPERTY(double narrowEndToEndMs READ narrowEndToEndMs NOTIFY statsChanged)		// 狭义端到端平均耗时(读帧->推理流程返回)
	Q_PROPERTY(double broadEndToEndMs READ broadEndToEndMs NOTIFY statsChanged)		// 广义端到端平均耗时(读帧->渲染/保存完成)
	Q_PROPERTY(int recordedFrames READ recordedFrames NOTIFY statsChanged)				// 窗口内记录帧数(成功+失败)
	Q_PROPERTY(int successfulFrames READ successfulFrames NOTIFY statsChanged)			// 成功帧数
	Q_PROPERTY(int failedFrames READ failedFrames NOTIFY statsChanged)					// 失败帧数
	Q_PROPERTY(double windowDurationMs READ windowDurationMs NOTIFY statsChanged)		// 统计窗口实测时长
	Q_PROPERTY(int snapshotIndex READ snapshotIndex NOTIFY statsChanged)				// 快照序号(切级归零)
	Q_PROPERTY(QVariantList stageStats READ stageStats NOTIFY statsChanged)				// 阶段统计行(已按级别过滤)

	// 运行上下文
	Q_PROPERTY(bool isRunning READ isRunning NOTIFY contextChanged )
	Q_PROPERTY(quint64 jobId READ jobId NOTIFY contextChanged)							// 当前作业ID(0=空闲)
	Q_PROPERTY(QString preferredModeText READ preferredModeText NOTIFY contextChanged)	// 用户选择的模式
	Q_PROPERTY(QString activeModeText READ activeModeText NOTIFY contextChanged)		// 实际生效的模式

	// 配置
	Q_PROPERTY(int levelIndex READ levelIndex WRITE setLevelIndex NOTIFY configChanged)	// 监控级别(PerformanceLevel 枚举值)
	Q_PROPERTY(int reportInterval READ reportInterval WRITE setReportInterval NOTIFY configChanged)	// 上报间隔(ms)

	// 横幅显示信息
	Q_PROPERTY(QString bannerText READ bannerText NOTIFY bannerChanged)
	Q_PROPERTY(BannerLevel bannerLevel READ bannerLevel NOTIFY bannerChanged)

public:
	// 横幅级别
	enum BannerLevel {
		BannerNone = 0,
		BannerWarning,
		BannerError
	};
	Q_ENUM(BannerLevel)

	explicit PerformanceApp(const PerformanceConfig& config, QObject* parent = nullptr );
	~PerformanceApp() override = default;

	Q_INVOKABLE PerformanceConfig config() const;
	Q_INVOKABLE void setConfig( const PerformanceConfig& config );
	Q_INVOKABLE void dismissBanner() {
		if (m_bannerText.isEmpty())
			return;
		m_bannerText.clear();
		m_bannerLevel = BannerNone;
		emit bannerChanged();
	}

	// 借用监测器
	IPerformanceMonitor* monitor();

	// 快照属性 getter
	float overallFPS() const { return m_overallFPS; }
	double narrowEndToEndMs() const { return m_narrowEndToEndMs; }
	double broadEndToEndMs() const { return m_broadEndToEndMs; }
	int recordedFrames() const { return m_recordedFrames; }
	int successfulFrames() const { return m_successfulFrames; }
	int failedFrames() const { return m_failedFrames; }
	double windowDurationMs() const { return m_windowDurationMs; }
	int snapshotIndex() const { return m_snapshotIndex; }
	QVariantList stageStats() const { return m_stageStats; }

	// 运行上下文 getter
	bool isRunning() const { return m_isRunning; }
	quint64 jobId() const { return m_jobId; }
	QString preferredModeText() const { return m_preferredModeText; }
	QString activeModeText() const { return m_activeModeText; }

	// 配置 getter / setter(QML 写入口)
	int levelIndex() const { return static_cast<int>( m_config.m_level ); }
	void setLevelIndex( int level );
	int reportInterval() const { return m_config.m_reportInterval; }
	void setReportInterval( int interval );

	// 运行上下文

	void setIsRunning( bool running );
	void setCurrentJobId( quint64 jobId ) {
		if (m_jobId != jobId) { m_jobId = jobId; emit contextChanged(); }
	}
	void setFlowModeTexts( QStringView preferred, QStringView active ) {
		const QString p = preferred.toString(), a = active.toString();
		if (m_preferredModeText != p || m_activeModeText != a) {
			m_preferredModeText = p;
			m_activeModeText = a;
			emit contextChanged();
		}
	}

	// 横幅 getter
	QString bannerText() const { return m_bannerText; }
	BannerLevel bannerLevel() const { return m_bannerLevel; }

signals:
	void configChanged();
	void statsChanged();	// 摊平属性刷新
	void contextChanged();	// 运行上下文刷新(jobId / 模式)
	void bannerChanged();	// 横幅刷新
	void errorOccurred(const AppError& error);	// 性能监控错误进行提醒

public slots:
	// 接收监测器快照，通知 QML
	void onSnapshotReady( const PerformanceSnapshot& snapshot );
	// 发生错误，横幅提醒和日志记录
	void onErrorOccurred( const AppError& error );

private:
	void showBanner(QStringView text, BannerLevel level);	// 统一的横幅出口

	MonitorPtr m_monitor{ nullptr };	// 性能监控器
	PerformanceConfig m_config;	// 性能监控配置

	// 摊平后的快照数据
	float m_overallFPS = 0.0f;
	double m_narrowEndToEndMs = 0.0;
	double m_broadEndToEndMs = 0.0;
	int m_recordedFrames = 0;
	int m_successfulFrames = 0;
	int m_failedFrames = 0;
	double m_windowDurationMs = 0.0;
	int m_snapshotIndex = 0;
	QVariantList m_stageStats;	// 阶段统计行 {name, averageMs, maximumMs, sampleCount}

	// 运行上下文
	bool m_isRunning = false; // 是否有作业运行，有才进行快照统计和发送信号
	quint64 m_jobId = 0;					// 当前作业ID(0=空闲)
	QString m_preferredModeText = "CPU";	// 用户选择的模式
	QString m_activeModeText = "CPU";		// 实际生效的模式

	// 横幅
	QString m_bannerText;	// 最近一条横幅文本
	BannerLevel m_bannerLevel = BannerNone;
};
