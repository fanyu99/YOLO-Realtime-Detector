#include "AppConfigWriter.h"
#include <cmath>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include "Logger.h"

namespace {

const QString kDetectorSection = QStringLiteral ( "detector" );
const QString kPerformanceSection = QStringLiteral ( "performance" );
const QString kDisplaySection = QStringLiteral ( "display" );
const QString kPluginsSection = QStringLiteral ( "plugins" );

// 枚举 -> 文本，与 loader 的字符串解析互逆
QString flowModeToText ( FlowMode mode ) {
	return mode == FlowMode::GPU ? QStringLiteral ( "gpu" ) : QStringLiteral ( "cpu" );
}

QString viewModeToText ( ViewMode mode ) {
	return mode == ViewMode::Realtime ? QStringLiteral ( "realtime" ) : QStringLiteral ( "offline" );
}

QString levelToText ( PerformanceLevel level ) {
	switch (level) {
	case PerformanceLevel::Off:		 return QStringLiteral ( "off" );
	case PerformanceLevel::Minimum:	 return QStringLiteral ( "minimum" );
	case PerformanceLevel::Basic:	 return QStringLiteral ( "basic" );
	case PerformanceLevel::Detailed: return QStringLiteral ( "detailed" );
	case PerformanceLevel::Full:	 return QStringLiteral ( "full" );
	}
	return QStringLiteral ( "basic" );
}

// 绝对路径 -> 相对配置目录的路径；目录之外(含跨盘)时保留绝对路径。
// 与 loader 的「相对配置目录 -> 绝对」互逆，保证整目录拷贝后仍能解析。
QString toConfigRelativePath ( const std::filesystem::path& path, const QDir& baseDir ) {
	if (path.empty ())
		return QString ();
	const QString absolute =
		QDir::fromNativeSeparators ( QString::fromStdWString ( path.wstring () ) );
	if (!QDir::isAbsolutePath ( absolute ))
		return absolute;
	const QString relative = baseDir.relativeFilePath ( absolute );
	// 跨盘符时 Qt 直接返回绝对路径；".." 开头说明落在配置目录之外
	if (QDir::isAbsolutePath ( relative ) || relative.startsWith ( QStringLiteral ( ".." ) ))
		return QDir::cleanPath ( absolute );
	return relative;
}

// ---- 与 readString / readInt / readDouble / readArray 对应的写入 ----
void writeString ( QJsonObject& obj, const QString& key, const QString& value ) {
	obj.insert ( key, value );
}

void writeInt ( QJsonObject& obj, const QString& key, int value ) {
	obj.insert ( key, value );
}

// 阈值以 6 位小数落盘：float 提升到 double 会带出尾数噪声(0.7f -> 0.699999988...)
void writeDouble ( QJsonObject& obj, const QString& key, double value ) {
	obj.insert ( key, std::round ( value * 1e6 ) / 1e6 );
}

void writeStringArray ( QJsonObject& obj, const QString& key, const QVector<QString>& values ) {
	QJsonArray array;
	for (const QString& value : values)
		array.append ( value );
	obj.insert ( key, array );
}

// 与 loadDetectorConfig 对称：同样的键、同样的取值范围
QJsonObject detectorToJson ( const DetectorConfig& cfg, const QDir& baseDir ) {
	QJsonObject obj;
	writeString ( obj, QStringLiteral ( "model_path" ), toConfigRelativePath ( cfg.model_path, baseDir ) );
	writeStringArray ( obj, QStringLiteral ( "class_names" ), cfg.class_names );
	writeDouble ( obj, QStringLiteral ( "conf_threshold" ), cfg.conf_threshold );
	writeDouble ( obj, QStringLiteral ( "iou_threshold" ), cfg.iou_threshold );
	writeInt ( obj, QStringLiteral ( "num_threads" ), cfg.num_threads );
	writeInt ( obj, QStringLiteral ( "device_id" ), cfg.device_id );
	writeInt ( obj, QStringLiteral ( "input_width" ), cfg.input_width );
	writeInt ( obj, QStringLiteral ( "input_height" ), cfg.input_height );
	writeString ( obj, QStringLiteral ( "preferred_mode" ), flowModeToText ( cfg.preferred_mode ) );
	// active_mode 是运行期状态(由 Session 回写)，不落盘，读取时重新推导
	return obj;
}

// 与 loadPerformanceConfig 对称
QJsonObject performanceToJson ( const PerformanceConfig& cfg ) {
	QJsonObject obj;
	writeString ( obj, QStringLiteral ( "monitor_level" ), levelToText ( cfg.level () ) );
	writeInt ( obj, QStringLiteral ( "report_interval_ms" ), cfg.reportInterval () );
	return obj;
}

// 与 loadDisplayConfig 对称：只写用户偏好，active_mode 与 backend 是运行态不落盘
QJsonObject displayToJson ( const DisplayPolicy& policy ) {
	QJsonObject obj;
	writeString ( obj, QStringLiteral ( "view_mode" ), viewModeToText ( policy.preferredMode () ) );
	return obj;
}

// 与 loadPluginConfigs 对称：每项 {type, pluginId, enabled, exists}
QJsonObject pluginToJson ( const PluginConfig& cfg ) {
	QJsonObject obj;
	writeString ( obj, QStringLiteral ( "type" ), pluginTypeToString ( cfg.type ) );
	writeString ( obj, QStringLiteral ( "pluginId" ), cfg.pluginId );
	obj.insert ( QStringLiteral ( "enabled" ), cfg.enabled );
	obj.insert ( QStringLiteral ( "exists" ), cfg.exists );
	return obj;
}

QJsonArray pluginsToJson ( const QVector<PluginConfig>& plugins ) {
	QJsonArray array;
	for (const PluginConfig& cfg : plugins)
		array.append ( pluginToJson ( cfg ) );
	return array;
}

// 只做数值/结构合法性检查：语义校验(模型是否存在、类别是否为空)由 App 侧负责
bool validateDetectorConfig ( const DetectorConfig& cfg, QString& error ) {
	if (cfg.model_path.empty ()) {
		error = QStringLiteral ( "detector.model_path: 不能为空" );
		return false;
	}
	const auto inUnitRange = [] ( float v ) {
		return std::isfinite ( v ) && v >= 0.0f && v <= 1.0f;
		};
	if (!inUnitRange ( cfg.conf_threshold )) {
		error = QStringLiteral ( "detector.conf_threshold: 应在 [0,1] 内，当前 %1" )
			.arg ( static_cast<double>( cfg.conf_threshold ) );
		return false;
	}
	if (!inUnitRange ( cfg.iou_threshold )) {
		error = QStringLiteral ( "detector.iou_threshold: 应在 [0,1] 内，当前 %1" )
			.arg ( static_cast<double>( cfg.iou_threshold ) );
		return false;
	}
	if (cfg.num_threads < 0) {
		error = QStringLiteral ( "detector.num_threads: 不应为负数，当前 %1" ).arg ( cfg.num_threads );
		return false;
	}
	if (cfg.device_id < 0) {
		error = QStringLiteral ( "detector.device_id: 不应为负数，当前 %1" ).arg ( cfg.device_id );
		return false;
	}
	if (cfg.input_width <= 0 || cfg.input_height <= 0) {
		error = QStringLiteral ( "detector.input_width/input_height: 应为正整数，当前 %1x%2" )
			.arg ( cfg.input_width ).arg ( cfg.input_height );
		return false;
	}
	return true;
}

bool validatePerformanceConfig ( const PerformanceConfig& cfg, QString& error ) {
	if (cfg.reportInterval () <= 0) {
		error = QStringLiteral ( "performance.report_interval_ms: 应为正整数，当前 %1" )
			.arg ( cfg.reportInterval () );
		return false;
	}
	return true;
}

bool validateDisplayConfig ( const DisplayPolicy& policy, QString& error ) {
	const ViewMode mode = policy.preferredMode ();
	if (mode != ViewMode::Offline && mode != ViewMode::Realtime) {
		error = QStringLiteral ( "display.view_mode: 不是合法的预览模式，当前 %1" )
			.arg ( static_cast<int>( mode ) );
		return false;
	}
	return true;
}

bool validatePluginConfigs ( const QVector<PluginConfig>& plugins, QString& error ) {
	for (int i = 0; i < plugins.size (); ++i) {
		const PluginConfig& cfg = plugins.at ( i );
		if (cfg.type != PluginType::FrameSource && cfg.type != PluginType::MemoryMonitor) {
			error = QStringLiteral ( "plugins[%1].type: 不是合法的插件类型" ).arg ( i );
			return false;
		}
		if (cfg.pluginId.trimmed ().isEmpty ()) {
			error = QStringLiteral ( "plugins[%1].pluginId: 不能为空" ).arg ( i );
			return false;
		}
		for (int j = 0; j < i; ++j) {
			if (plugins.at ( j ).type == cfg.type && plugins.at ( j ).pluginId == cfg.pluginId) {
				error = QStringLiteral ( "plugins[%1].pluginId: 与前面的项重复 -> %2" )
					.arg ( i ).arg ( cfg.pluginId );
				return false;
			}
		}
	}
	return true;
}

// 读回磁盘上的既有文档：写入是「读旧 + 改写自己负责的节」，
// 这样文件的未知键与将来新增的节不会被静默抹掉。文件不存在/不可解析时返回空对象。
QJsonObject readExistingRoot ( const QString& filePath ) {
	QFile file ( filePath );
	if (!file.open ( QIODevice::ReadOnly | QIODevice::Text ))
		return QJsonObject ();
	QByteArray data = file.readAll ();
	file.close ();

	static const QByteArray kBom = QByteArray::fromHex ( "EFBBBF" );
	if (data.startsWith ( kBom ))
		data.remove ( 0, kBom.size () );

	QJsonParseError parseError{};
	const QJsonDocument doc = QJsonDocument::fromJson ( data, &parseError );
	if (parseError.error != QJsonParseError::NoError || !doc.isObject ()) {
		Logger::instance ()->warning ( QStringLiteral ( "[AppConfigWriter]: 现有配置无法解析(%1)，将整体重写: %2" )
			.arg ( parseError.errorString (), filePath ) );
		return QJsonObject ();
	}
	return doc.object ();
}

// 原子落盘：QSaveFile 先写临时文件，commit() 时再替换目标，避免写一半留下坏文件
bool writeJsonFile ( const QString& filePath, const QJsonObject& root, QString& error ) {
	const QDir dir = QFileInfo ( filePath ).absoluteDir ();
	if (!dir.exists () && !QDir ().mkpath ( dir.absolutePath () )) {
		error = QStringLiteral ( "配置目录无法创建: %1" ).arg ( dir.absolutePath () );
		return false;
	}

	QSaveFile file ( filePath );
	if (!file.open ( QIODevice::WriteOnly | QIODevice::Text )) {
		error = QStringLiteral ( "配置文件无法写入: %1 (%2)" ).arg ( filePath, file.errorString () );
		return false;
	}
	// UTF-8 无 BOM，缩进格式(与手写配置一致)
	file.write ( QJsonDocument ( root ).toJson ( QJsonDocument::Indented ) );
	file.write ( "\n" );
	if (!file.commit ()) {
		error = QStringLiteral ( "配置文件写入失败: %1 (%2)" ).arg ( filePath, file.errorString () );
		return false;
	}
	return true;
}

// 落盘并重试一次：杀软扫描/索引/占用会造成瞬时写入失败，重试一次通常即可成功
bool writeJsonFileWithRetry ( const QString& filePath, const QJsonObject& root, QString& error ) {
	if (writeJsonFile ( filePath, root, error ))
		return true;
	Logger::instance ()->warning ( QStringLiteral ( "[AppConfigWriter]: 写入失败，重试一次: " ) + error );
	return writeJsonFile ( filePath, root, error );
}

// 写入流程的公共尾部：读回旧文档 -> 覆盖指定节 -> 原子落盘。
// 只替换自己的节，文件里的其它节与未知键原样保留。
bool commitSections ( const QString& filePath,
	const QVector<QPair<QString, QJsonValue>>& sections, QString& error ) {
	QJsonObject root = readExistingRoot ( filePath );
	for (const auto& section : sections)
		root.insert ( section.first, section.second );
	return writeJsonFileWithRetry ( filePath, root, error );
}

} // namespace

AppConfigWriter::AppConfigWriter ( QStringView filePath )
	: configFilePath ( QUrl::fromLocalFile ( filePath.toString () ) ) {
}

AppConfigWriter* AppConfigWriter::initialize ( QStringView filePath ) {
	if (!m_instance)
		m_instance.reset (new AppConfigWriter ( filePath ));
	else {
		Logger::instance ()->warning (QStringLiteral("[AppConfigWriter]: 重复初始化，已忽略"));
	}
	return m_instance.get();
}

AppConfigWriter* AppConfigWriter::instance () {
	if (!m_instance)
		Logger::instance ()->fatal ( QStringLiteral ( "[AppConfigWriter]: 未初始化，必须先调用 initialize()!" ) );
	return m_instance.get ();
}

void AppConfigWriter::setConfigFilePath ( const QUrl& path ) {
	std::lock_guard<std::mutex> lock ( m_mutex );
	configFilePath = path;
}

QString AppConfigWriter::errorMessage () const {
	std::lock_guard<std::mutex> lock ( m_mutex );
	return m_errorMessage;
}

bool AppConfigWriter::fail ( const QString& reason ) {
	m_errorMessage = reason;
	Logger::instance ()->error ( QStringLiteral ( "[AppConfigWriter]: 配置未写入，" ) + reason );
	return false;
}

bool AppConfigWriter::writeAppConfig ( const AppConfig& config ) {
	std::lock_guard<std::mutex> lock ( m_mutex );
	m_errorMessage.clear ();

	const QString filePath = configFilePath.toLocalFile ();
	if (filePath.isEmpty ())
		return fail ( QStringLiteral ( "配置文件路径为空(未 initialize 或路径不是本地文件)" ) );

	QString error;
	if (!validateDetectorConfig ( config.detectorConfig, error )
		|| !validatePerformanceConfig ( config.performanceConfig, error )
		|| !validateDisplayConfig ( config.displayPolicy, error )
		|| !validatePluginConfigs ( config.pluginConfigs, error ))
		return fail ( error );

	const QDir baseDir = QFileInfo ( filePath ).absoluteDir ();
	const QVector<QPair<QString, QJsonValue>> sections{
		{ kDetectorSection, detectorToJson ( config.detectorConfig, baseDir ) },
		{ kPerformanceSection, performanceToJson ( config.performanceConfig ) },
		{ kDisplaySection, displayToJson ( config.displayPolicy ) },
		{ kPluginsSection, pluginsToJson ( config.pluginConfigs ) } };

	if (!commitSections ( filePath, sections, error ))
		return fail ( error );

	Logger::instance ()->success ( QStringLiteral ( "[AppConfigWriter]: 应用配置已写入 -> " ) + filePath );
	return true;
}

bool AppConfigWriter::writeDetectorConfig ( const DetectorConfig& config ) {
	std::lock_guard<std::mutex> lock ( m_mutex );
	m_errorMessage.clear ();

	const QString filePath = configFilePath.toLocalFile ();
	if (filePath.isEmpty ())
		return fail ( QStringLiteral ( "配置文件路径为空(未 initialize 或路径不是本地文件)" ) );

	QString error;
	if (!validateDetectorConfig ( config, error ))
		return fail ( error );

	const QDir baseDir = QFileInfo ( filePath ).absoluteDir ();
	const QVector<QPair<QString, QJsonValue>> sections{
		{ kDetectorSection, detectorToJson ( config, baseDir ) } };

	if (!commitSections ( filePath, sections, error ))
		return fail ( error );

	Logger::instance ()->success ( QStringLiteral ( "[AppConfigWriter]: 检测器配置已写入 -> " ) + filePath );
	return true;
}

bool AppConfigWriter::writePerformanceConfig ( const PerformanceConfig& config ) {
	std::lock_guard<std::mutex> lock ( m_mutex );
	m_errorMessage.clear ();

	const QString filePath = configFilePath.toLocalFile ();
	if (filePath.isEmpty ())
		return fail ( QStringLiteral ( "配置文件路径为空(未 initialize 或路径不是本地文件)" ) );

	QString error;
	if (!validatePerformanceConfig ( config, error ))
		return fail ( error );

	const QVector<QPair<QString, QJsonValue>> sections{
		{ kPerformanceSection, performanceToJson ( config ) } };

	if (!commitSections ( filePath, sections, error ))
		return fail ( error );

	Logger::instance ()->success ( QStringLiteral ( "[AppConfigWriter]: 性能配置已写入 -> " ) + filePath );
	return true;
}

bool AppConfigWriter::writeDisplayConfig ( const DisplayPolicy& policy ) {
	std::lock_guard<std::mutex> lock ( m_mutex );
	m_errorMessage.clear ();

	const QString filePath = configFilePath.toLocalFile ();
	if (filePath.isEmpty ())
		return fail ( QStringLiteral ( "配置文件路径为空(未 initialize 或路径不是本地文件)" ) );

	QString error;
	if (!validateDisplayConfig ( policy, error ))
		return fail ( error );

	const QVector<QPair<QString, QJsonValue>> sections{
		{ kDisplaySection, displayToJson ( policy ) } };

	if (!commitSections ( filePath, sections, error ))
		return fail ( error );

	Logger::instance ()->success ( QStringLiteral ( "[AppConfigWriter]: 显示配置已写入 -> " ) + filePath );
	return true;
}

bool AppConfigWriter::writePluginConfigs ( const QVector<PluginConfig>& plugins ) {
	std::lock_guard<std::mutex> lock ( m_mutex );
	m_errorMessage.clear ();

	const QString filePath = configFilePath.toLocalFile ();
	if (filePath.isEmpty ())
		return fail ( QStringLiteral ( "配置文件路径为空(未 initialize 或路径不是本地文件)" ) );

	QString error;
	if (!validatePluginConfigs ( plugins, error ))
		return fail ( error );

	const QVector<QPair<QString, QJsonValue>> sections{
		{ kPluginsSection, pluginsToJson ( plugins ) } };

	if (!commitSections ( filePath, sections, error ))
		return fail ( error );

	Logger::instance ()->success ( QStringLiteral ( "[AppConfigWriter]: 插件配置已写入 -> " ) + filePath );
	return true;
}
