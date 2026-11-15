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

// 性能app，管理性能监控器的生命周期，快照属性由QML绑定
// 错误使用banner进行提醒
class PerformanceApp : public QObject {
	Q_OBJECT
	QML_ELEMENT
	Q_PROPERTY(PerformanceConfig config READ config WRITE setConfig NOTIFY configChanged)

	// QML绑定属性
	Q_PROPERTY(float overallFPS READ overallFPS NOTIFY statsChanged)					// 整体帧率
	Q_PROPERTY(double averageEndToEndMs READ averageEndToEndMs NOTIFY statsChanged)		// 单帧端到端平均耗时
	Q_PROPERTY(int recordedFrames READ recordedFrames NOTIFY statsChanged)				// 窗口内记录帧数(成功+失败)
	Q_PROPERTY(int successfulFrames READ successfulFrames NOTIFY statsChanged)			// 成功帧数
	Q_PROPERTY(int failedFrames READ failedFrames NOTIFY statsChanged)					// 失败帧数
	Q_PROPERTY(double windowDurationMs READ windowDurationMs NOTIFY statsChanged)		// 统计窗口实测时长
	Q_PROPERTY(int snapshotIndex READ snapshotIndex NOTIFY statsChanged)				// 快照序号(切级归零)
	Q_PROPERTY(QVariantList stageStats READ stageStats NOTIFY statsChanged)				// 阶段统计行(已按级别过滤)

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

	// 借用监测器(供组合根接线：Manager -> Worker -> Session -> ScopedStageTimer)
	IPerformanceMonitor* monitor();

	// 快照属性 getter
	float overallFPS() const { return m_overallFPS; }
	double averageEndToEndMs() const { return m_averageEndToEndMs; }
	int recordedFrames() const { return m_recordedFrames; }
	int successfulFrames() const { return m_successfulFrames; }
	int failedFrames() const { return m_failedFrames; }
	double windowDurationMs() const { return m_windowDurationMs; }
	int snapshotIndex() const { return m_snapshotIndex; }
	QVariantList stageStats() const { return m_stageStats; }

	// 横幅 getter
	QString bannerText() const { return m_bannerText; }
	BannerLevel bannerLevel() const { return m_bannerLevel; }

signals:
	void configChanged();
	void statsChanged();	// 摊平属性刷新
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
	double m_averageEndToEndMs = 0.0;
	int m_recordedFrames = 0;
	int m_successfulFrames = 0;
	int m_failedFrames = 0;
	double m_windowDurationMs = 0.0;
	int m_snapshotIndex = 0;
	QVariantList m_stageStats;	// 阶段统计行 {name, averageMs, maximumMs, sampleCount}

	// 横幅
	QString m_bannerText;	// 最近一条横幅文本
	BannerLevel m_bannerLevel = BannerNone;
};
