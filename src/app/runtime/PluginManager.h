#pragma once
#include <QString>
#include <QStringList>
#include "FrameSource.h"
#include "FrameSourcePluginInterface.h"
#include "MemoryMonitorPluginInterface.h"
#include <QHash>
#include <QVariantHash>
#include <QPluginLoader>
#include <memory>
#include <vector>
// 插件类型
enum class PluginType {
	FrameSource,	   // 帧源插件
	MemoryMonitor,   // 内存监控插件
Count // 数量
};
inline QString pluginTypeToString ( PluginType type ) {
	switch (type) {
	case PluginType::FrameSource: return QStringLiteral ( "FrameSource" );
	case PluginType::MemoryMonitor: return QStringLiteral ( "MemoryMonitor" );
	default: return QStringLiteral ( "Unknown" );
	}
}
using LoaderPtr = std::unique_ptr<QPluginLoader>;
// 插件管理器
// 当前仅帧源插件，后续进行拓展其他类型插件
class PluginManager {
public:
using PluginManagerPtr = std::unique_ptr<PluginManager>;
	PluginManager ( const PluginManager& ) = delete;
	PluginManager& operator= ( const PluginManager& ) = delete;
	~PluginManager ();
	// 初始化插件并返回单例，必须先调用 initialize() 初始化插件路径后才能调用 instance()
	static PluginManager* initialize ( const QStringList& pluginPaths );
	static PluginManager* instance ();

	// 指定插件是否存在
	bool pluginExists ( const QString& pluginId ) const {
		return framesourcePlugins.contains ( pluginId );
	}
	// 指定内存监控插件是否存在
	bool memoryMonitorPluginExists ( const QString& pluginId ) const {
		return memoryMonitorPlugins.contains ( pluginId );
	}
	// 获取插件ID列表
	QStringList enumeratePluginIds () const {
		return framesourcePlugins.keys () + memoryMonitorPlugins.keys ();
	}

	// 返回帧源插件，不存在返回nullptr
	FrameSourcePluginInterface* getFrameSourcePlugin (QStringView pluginId ) const {
		return framesourcePlugins.value ( pluginId, nullptr );
	}
	// 返回内存监控插件，不存在返回nullptr
	MemoryMonitorPluginInterface* getMemoryMonitorPlugin ( QStringView pluginId ) const {
		return memoryMonitorPlugins.value ( pluginId, nullptr );
	}

private:
	explicit PluginManager (const QStringList& pluginPaths);
	// TODO: 等接入app.json配置后再将帧源插件和内存监控插件接入到插件表
	// 插件表,按照PluginType下标访问
	// PluginType对应的插件表是QHash<pluginId, pluginInterface*>
	QVector<QVariantHash>pluginsMap;

	// 1. 帧源插件：
	std::vector<LoaderPtr> pluginLoaders; // 帧源插件加载器(unique_ptr，使用std::vector，不使用QVector)
	QHash<QString, FrameSourcePluginInterface*> framesourcePlugins; // pluginId : 插件
	// 2. 内存监控插件
	QHash<QString, MemoryMonitorPluginInterface*> memoryMonitorPlugins; // pluginId : 插件



	inline static PluginManagerPtr m_instance{nullptr}; // 单例
};
