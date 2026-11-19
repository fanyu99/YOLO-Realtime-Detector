#pragma once
#include <optional>
#include <QStringList>
#include <QString>
#include <QObject>
#include "DetectionTypes.h"
#include "PerformanceTypes.h"
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
    "performance": {
        "monitor_level": "detailed",
        "report_interval_ms": 500
    }
}

*/


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
