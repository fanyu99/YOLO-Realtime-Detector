#pragma once
#include <optional>
#include <cmath>
#include <QString>
#include <QStringList>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include "AppConfig.h"

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
*/

// 应用配置读取器
class AppConfigLoader {
public:
	static AppConfig* loadAppConfig(const QString& configFilePath, QObject* parent = nullptr) {
		QStringList warnings;
		QFile file(configFilePath);
		if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
			return makeFailed(parent, QStringLiteral("配置文件无法打开: %1").arg(configFilePath));
		QString text = QString::fromUtf8(file.readAll());
		file.close();
		if (!text.isEmpty() && text.at(0) == QChar(0xFEFF))
			text.remove(0, 1);

		QJsonParseError parseError{};
		const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &parseError);
		if (parseError.error != QJsonParseError::NoError || !doc.isObject())
			return makeFailed(parent, QStringLiteral("JSON 解析失败(偏移 %1): %2")
				.arg(parseError.offset)
				.arg(parseError.errorString()));
		const QJsonObject root = doc.object();

		const QDir baseDir = QFileInfo(configFilePath).absoluteDir();

		const QJsonValue detectorValue = root.value("detector");
		DetectorConfig detectorConfig;
		if (detectorValue.isObject())
			detectorConfig = loadDetectorConfig(detectorValue.toObject(), baseDir, warnings);
		else if (!detectorValue.isUndefined() && !detectorValue.isNull())
			warn("app", "detector", QStringLiteral("应为对象，该节已按默认值处理"), warnings);

		const QJsonValue performanceValue = root.value("performance");
		PerformanceConfig performanceConfig;
		if (performanceValue.isObject())
			performanceConfig = loadPerformanceConfig(performanceValue.toObject(), warnings);
		else if (!performanceValue.isUndefined() && !performanceValue.isNull())
			warn("app", "performance", QStringLiteral("应为对象，该节已按默认值处理"), warnings);

		const QJsonValue displayValue = root.value("display");
		DisplayPolicy displayPolicy;
		if (displayValue.isObject())
			displayPolicy = loadDisplayConfig(displayValue.toObject(), warnings);
		else if (!displayValue.isUndefined() && !displayValue.isNull())
			warn("app", "display", QStringLiteral("应为对象，该节已按默认值处理"), warnings);

		for (const QString& key : root.keys()) {
			if (key != "detector" && key != "performance" && key != "display")
				warn("app", key, QStringLiteral("未知键，已忽略"), warnings);
		}
		auto* cfg = new AppConfig(detectorConfig, performanceConfig, displayPolicy, parent);
		cfg->warnings = warnings;
		return cfg;
	}

private:
	// 构造一份带失败原因的默认配置（保证调用方总能拿到非空对象）
	static AppConfig* makeFailed(QObject* parent, const QString& warning) {
		auto* cfg = new AppConfig(DetectorConfig{}, PerformanceConfig{}, DisplayPolicy{}, parent);
		cfg->warnings << warning;
		return cfg;
	}

	// 警告
	static void warn(const QString& section, const QString& key,
		const QString& msg, QStringList& warnings) {
		warnings << QStringLiteral("%1.%2: %3").arg(section, key, msg);
	}
	// 检查对象中是否有未知键
	static void checkUnknownKeys(const QJsonObject& obj, const QString& section,
		const QStringList& knownKeys, QStringList& warnings) {
		for (const QString& key : obj.keys()) {
			if (!knownKeys.contains(key))
				warn(section, key, QStringLiteral("未知键，已忽略"), warnings);
		}
	}
	// 读取字符串、数值、整数、数组
	static std::optional<QString> readString(const QJsonObject& obj, const QString& key,
		const QString& section, QStringList& warnings) {
		const QJsonValue v = obj.value(key);
		if (v.isUndefined() || v.isNull())
			return std::nullopt;
		if (!v.isString()) {
			warn(section, key, QStringLiteral("应为字符串，已回退默认值"), warnings);
			return std::nullopt;
		}
		return v.toString();
	}

	static std::optional<double> readDouble(const QJsonObject& obj, const QString& key,
		const QString& section, QStringList& warnings) {
		const QJsonValue v = obj.value(key);
		if (v.isUndefined() || v.isNull())
			return std::nullopt;
		if (!v.isDouble()) {
			warn(section, key, QStringLiteral("应为数值，已回退默认值"), warnings);
			return std::nullopt;
		}
		return v.toDouble();
	}

	static std::optional<int> readInt(const QJsonObject& obj, const QString& key,
		const QString& section, QStringList& warnings) {
		const QJsonValue v = obj.value(key);
		if (v.isUndefined() || v.isNull())
			return std::nullopt;
		if (!v.isDouble()) {
			warn(section, key, QStringLiteral("应为数值，已回退默认值"), warnings);
			return std::nullopt;
		}
		const double d = v.toDouble();
		if (d != std::floor(d) || d < -2147483648.0 || d > 2147483647.0) {
			warn(section, key, QStringLiteral("应为整数，已回退默认值"), warnings);
			return std::nullopt;
		}
		return static_cast<int>(d);
	}

	static std::optional<QJsonArray> readArray(const QJsonObject& obj, const QString& key,
		const QString& section, QStringList& warnings) {
		const QJsonValue v = obj.value(key);
		if (v.isUndefined() || v.isNull())
			return std::nullopt;
		if (!v.isArray()) {
			warn(section, key, QStringLiteral("应为数组，已回退默认值"), warnings);
			return std::nullopt;
		}
		return v.toArray();
	}
	// 检测器配置
	static DetectorConfig loadDetectorConfig(const QJsonObject& obj, const QDir& baseDir,
		QStringList& warnings) {
		const QString section = QStringLiteral("detector");
		DetectorConfig cfg;

		if (auto path = readString(obj, "model_path", section, warnings)) {
			if (!path->trimmed().isEmpty()) {
				const QString absolute =
					QFileInfo(baseDir.filePath(*path)).absoluteFilePath();
				cfg.model_path = absolute.toStdWString();
			}
		}

		if (auto arr = readArray(obj, "class_names", section, warnings)) {
			QVector<QString> names;
			for (const QJsonValue& v : *arr) {
				if (v.isString())
					names << v.toString();
				else
					warn(section, "class_names", QStringLiteral("数组元素应为字符串，已跳过该项"), warnings);
			}
			cfg.class_names = names;
		}

		if (auto v = readDouble(obj, "conf_threshold", section, warnings)) {
			if (*v >= 0.0 && *v <= 1.0)
				cfg.conf_threshold = static_cast<float>(*v);
			else
				warn(section, "conf_threshold", QStringLiteral("应在 [0,1] 内，已回退默认值"), warnings);
		}

		if (auto v = readDouble(obj, "iou_threshold", section, warnings)) {
			if (*v >= 0.0 && *v <= 1.0)
				cfg.iou_threshold = static_cast<float>(*v);
			else
				warn(section, "iou_threshold", QStringLiteral("应在 [0,1] 内，已回退默认值"), warnings);
		}

		if (auto v = readInt(obj, "num_threads", section, warnings)) {
			if (*v >= 0)
				cfg.num_threads = *v;
			else
				warn(section, "num_threads", QStringLiteral("不应为负数，已回退默认值"), warnings);
		}

		if (auto v = readInt(obj, "device_id", section, warnings)) {
			if (*v >= 0)
				cfg.device_id = *v;
			else
				warn(section, "device_id", QStringLiteral("不应为负数，已回退默认值"), warnings);
		}

		if (auto v = readInt(obj, "input_width", section, warnings)) {
			if (*v > 0)
				cfg.input_width = *v;
			else
				warn(section, "input_width", QStringLiteral("应为正整数，已回退默认值"), warnings);
		}

		if (auto v = readInt(obj, "input_height", section, warnings)) {
			if (*v > 0)
				cfg.input_height = *v;
			else
				warn(section, "input_height", QStringLiteral("应为正整数，已回退默认值"), warnings);
		}

		if (auto mode = readString(obj, "preferred_mode", section, warnings)) {
			const QString m = mode->trimmed().toLower();
			if (m == "cpu")
				cfg.preferred_mode = DetectorConfig::FlowMode::CPU;
			else if (m == "gpu")
				cfg.preferred_mode = DetectorConfig::FlowMode::GPU;
			else
				warn(section, "preferred_mode", QStringLiteral("应为 cpu 或 gpu，已回退默认值"), warnings);
		}

		checkUnknownKeys(obj, section,
			{ "model_path", "class_names", "conf_threshold", "iou_threshold",
			  "num_threads", "device_id", "input_width", "input_height", "preferred_mode" },
			warnings);
		return cfg;
	}

	// 性能监控配置
	static PerformanceConfig loadPerformanceConfig(const QJsonObject& obj, QStringList& warnings) {
		const QString section = QStringLiteral("performance");
		PerformanceConfig cfg;

		if (auto level = readString(obj, "monitor_level", section, warnings)) {
			const QString l = level->trimmed().toLower();
			if (l == "off")
				cfg.setLevel(PerformanceLevel::Off);
			else if (l == "basic")
				cfg.setLevel(PerformanceLevel::Basic);
			else if (l == "detailed")
				cfg.setLevel(PerformanceLevel::Detailed);
			else if (l == "full")
				cfg.setLevel(PerformanceLevel::Full);
			else
				warn(section, "monitor_level", QStringLiteral("应为 off/basic/detailed/full，已回退默认值"), warnings);
		}

		if (auto v = readInt(obj, "report_interval_ms", section, warnings)) {
			if (*v > 0)
				cfg.setReportInterval(*v);
			else
				warn(section, "report_interval_ms", QStringLiteral("应为正整数，已回退默认值"), warnings);
		}

		checkUnknownKeys(obj, section, { "monitor_level", "report_interval_ms" }, warnings);
		return cfg;
	}

	// 显示配置：只读取用户偏好 view_mode，active_mode / backend 由运行期推导
	static DisplayPolicy loadDisplayConfig(const QJsonObject& obj, QStringList& warnings) {
		const QString section = QStringLiteral("display");
		DisplayPolicy policy;	// 默认离线预览

		if (auto mode = readString(obj, "view_mode", section, warnings)) {
			const QString m = mode->trimmed().toLower();
			if (m == "offline")
				policy.setViewMode(ViewMode::Offline);	// 顺带推导 backend
			else if (m == "realtime")
				policy.setViewMode(ViewMode::Realtime);
			else
				warn(section, "view_mode", QStringLiteral("应为 offline 或 realtime，已回退默认值"), warnings);
		}

		checkUnknownKeys(obj, section, { "view_mode" }, warnings);
		return policy;
	}
};
