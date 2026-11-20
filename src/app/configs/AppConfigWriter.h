#pragma once
#include <memory>
#include <mutex>
#include <QString>
#include <QUrl>
#include "AppConfig.h"
#include "DetectionTypes.h"
#include "DisplayPolicy.h"
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
	"display": {
		"view_mode": "offline"
	},
	"performance": {
		"monitor_level": "detailed",
		"report_interval_ms": 500
	}
}

display 节只落盘 view_mode(用户偏好)：active_mode 与 backend 都是运行态，不落盘。
*/



// 配置写入器
class AppConfigWriter {
public:
	using AppConfigWriterPtr = std::unique_ptr<AppConfigWriter>;
	static AppConfigWriter* instance ();
	static AppConfigWriter* initialize ( QStringView filePath );
	AppConfigWriter ( const AppConfigWriter& other ) = delete;
	AppConfigWriter& operator= ( const AppConfigWriter& other ) = delete;
	~AppConfigWriter () = default;

	// 写入整份配置
	bool writeAppConfig(const AppConfig& config);
	bool writeDetectorConfig(const DetectorConfig& config);
	// 只写入 performance 节：detector 与其它键按文件中的原样保留
	bool writePerformanceConfig ( const PerformanceConfig& config );
	// 只写入 display 节：其它节按文件中的原样保留
	bool writeDisplayConfig ( const DisplayPolicy& policy );
	void setConfigFilePath ( const QUrl& path);
	// 最近一次写入失败的原因(成功时为空)
	QString errorMessage () const;

private:
	inline static AppConfigWriterPtr m_instance{nullptr};
	explicit AppConfigWriter ( QStringView filePath );
	// 记录失败原因 + 写日志，统一返回 false
	bool fail ( const QString& reason );

	mutable std::mutex m_mutex; // 互斥锁，保护写入与错误信息
	QUrl configFilePath;	// 配置文件路径
	QString m_errorMessage{}; // 最近的写入错误信息
};
