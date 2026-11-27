#pragma once
#include <QString>
#include <QStringList>
#include "FrameSource.h"
#include "FrameSourcePluginInterface.h"
#include <QHash>
#include <QVector>
#include <QPluginLoader>
#include <memory>
#include <vector>
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
	// 获取插件ID列表
	QStringList enumeratePluginIds () const {
		return framesourcePlugins.keys ();
	}

	// 返回帧源插件，不存在返回nullptr
	FrameSourcePluginInterface* getFrameSourcePlugin (QStringView pluginId ) const {
		return framesourcePlugins.value ( pluginId, nullptr );
	}

private:
	explicit PluginManager (const QStringList& pluginPaths);
	// 1. 帧源插件：
	std::vector<LoaderPtr> pluginLoaders; // 帧源插件加载器；QVector 不支持 move-only 元素
	QHash<QString, FrameSourcePluginInterface*> framesourcePlugins; // pluginId : 插件



	inline static PluginManagerPtr m_instance{nullptr}; // 单例
};
