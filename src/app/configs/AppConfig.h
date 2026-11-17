#pragma once
#include <optional>
#include <QStringList>
#include <QString>
#include <QObject>
#include "DetectionTypes.h"
#include "PerformanceTypes.h"
// 程序配置
struct AppConfig : public QObject {
	Q_OBJECT
	Q_PROPERTY( DetectorConfig detectorConfig MEMBER detectorConfig CONSTANT )
	Q_PROPERTY( PerformanceConfig performanceConfig MEMBER performanceConfig CONSTANT )
	Q_PROPERTY( QStringList warnings MEMBER warnings CONSTANT )
public:
	AppConfig() = default;
	AppConfig( const DetectorConfig& detectorConfig, const PerformanceConfig& performanceConfig, QObject* parent = nullptr )
		: QObject( parent ), detectorConfig( detectorConfig ), performanceConfig( performanceConfig ) {}
	DetectorConfig detectorConfig{};
	PerformanceConfig performanceConfig{};
	QStringList warnings;	// 警告信息
};
