#include "PluginManager.h"
#include "Logger.h"


PluginManager* PluginManager::instance () {
	if (!m_instance)
		Logger::instance ()->fatal ( QStringLiteral ( "[PluginManager]：未初始化，必须先调用 initialize()!" ) );
	return m_instance.get ();
}

PluginManager::~PluginManager () = default;

PluginManager* PluginManager::initialize ( const QStringList& pluginPaths ) {
	if (!m_instance)
	{
		m_instance = PluginManagerPtr ( new PluginManager ( pluginPaths ) );
	}
	else
	{
		Logger::instance ()->warning ( QStringLiteral ( "[PluginManager]：PluginManager已初始化，重复调用initialize()" ) );
	}
	return m_instance.get ();
}

PluginManager::PluginManager ( const QStringList& pluginPaths ) {
	// 加载插件
	for (const auto& path : pluginPaths)
	{
		// TODO: 等待接入app.json配置文件后按照配置文件中的插件类型、路径进行加载
		// 当前仅加载帧源插件和内存监控插件
		LoaderPtr loader = std::make_unique<QPluginLoader> ( path );
		loader->setLoadHints ( QLibrary::PreventUnloadHint );	 // 防止插件被卸载
		if (loader->load ())
		{
			auto* plugin = qobject_cast<FrameSourcePluginInterface*> (loader->instance ());
			if (plugin)
			{
				const QString id = plugin->pluginId ();
				if (!framesourcePlugins.contains ( id ))
				{
					framesourcePlugins.insert ( id, plugin );
					pluginLoaders.push_back ( std::move ( loader ) );
					Logger::instance ()->info ( QStringLiteral ( "[PluginManager]：已加载帧源插件 -> " ) + id );
				}
				else
				{
					Logger::instance ()->warning ( QStringLiteral ( "[PluginManager]：重复的帧源插件ID -> " ) + id );
				}
			}
			else
			{
				auto* plugin2 = qobject_cast<MemoryMonitorPluginInterface*> (loader->instance ());
				if (plugin2)
				{
					const QString id = plugin2->pluginId ();
					if (!memoryMonitorPlugins.contains ( id ))
					{
						memoryMonitorPlugins.insert ( id, plugin2 );
						pluginLoaders.push_back ( std::move ( loader ) );
						Logger::instance ()->info ( QStringLiteral ( "[PluginManager]：已加载内存监控插件 -> " ) + id );
					}
					else
					{
						Logger::instance ()->warning ( QStringLiteral ( "[PluginManager]：重复的内存监控插件ID -> " ) + id );
					}
				}
				else
				{
					Logger::instance ()->warning ( QStringLiteral ( "[PluginManager]：插件不是帧源插件也不是内存监控插件 -> " ) + path );
				}
			}
		}
		// 插件加载失败
		else
		{
			Logger::instance ()->warning ( QStringLiteral ( "[PluginManager]：插件加载失败 -> " ) + path + QStringLiteral ( " : " ) + loader->errorString () );
		}
	}
}