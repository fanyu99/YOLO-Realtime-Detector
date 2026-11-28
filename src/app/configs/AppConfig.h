#pragma once
#include <QStringList>
#include <QString>
#include <QObject>
#include "DetectionTypes.h"
#include "DisplayPolicy.h"
#include "PerformanceTypes.h"
#include "PluginConfig.h"
#include <QVector>
/*
格式：
{
    "detector": {
        "class_names": [
            "person"
        ],
        "conf_threshold": 0.5,
        "device_id": 0,
        "input_height": 640,
        "input_width": 640,
        "iou_threshold": 0.5,
        "model_path": "D:/python/YOLO/runs/train/people_yolo26s_v2_e50/weights/best.onnx",
        "num_threads": 0,
        "preferred_mode": "gpu"
    },
    "display": {
        "view_mode": "offline"
    },
    "performance": {
        "monitor_level": "detailed",
        "report_interval_ms": 500
    },
    "plugins": [
        { "type": "FrameSource", "pluginId": "Hik", "enabled": true, "exists": true }
    ]
}

*/


// 程序配置
struct AppConfig : public QObject {
	Q_OBJECT
	Q_PROPERTY( DetectorConfig detectorConfig MEMBER detectorConfig CONSTANT )
	Q_PROPERTY( PerformanceConfig performanceConfig MEMBER performanceConfig CONSTANT )
	Q_PROPERTY( QStringList warnings MEMBER warnings CONSTANT )
	Q_PROPERTY( QString preferredViewMode READ preferredViewMode NOTIFY preferredViewModeChanged )
	Q_PROPERTY( QString activeViewMode READ activeViewMode NOTIFY activeViewModeChanged )
public:
	AppConfig() = default;
	AppConfig ( const DetectorConfig& detectorConfig, const PerformanceConfig& performanceConfig,
		const DisplayPolicy& displayPolicy, QObject* parent = nullptr )
		: QObject ( parent ), detectorConfig ( detectorConfig ), performanceConfig ( performanceConfig ), displayPolicy ( displayPolicy ) {}
	// 实际生效的预览模式
	QString activeViewMode() const {
		return ViewMode2String ( displayPolicy.activeMode() );
	}
	// 用户偏好的预览模式
	QString preferredViewMode() const {
		return ViewMode2String ( displayPolicy.preferredMode() );
	}
	// 设置模式
	void setViewMode ( const QString& newModeStr ) {
		const auto newMode = String2ViewMode ( newModeStr );
		if (!newMode) {
			warnings << QStringLiteral ( "预览模式无效，已忽略：" ) + newModeStr;
			return;
		}
		// 模式没变
		if (!displayPolicy.setViewMode ( *newMode ))
			return;		
		emit preferredViewModeChanged();
		emit activeViewModeChanged();
	}
	DetectorConfig detectorConfig{};
	PerformanceConfig performanceConfig{};
	DisplayPolicy displayPolicy{};	// 显示策略
	QVector<PluginConfig> pluginConfigs;	// 插件配置列表
	QStringList warnings;	// 警告信息
signals:
	void preferredViewModeChanged();
	void activeViewModeChanged();
};
