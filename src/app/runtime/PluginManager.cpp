#include "PluginManager.h"
#include "Logger.h"
#include <QJsonObject>
#include <optional>

namespace {
	size_t getIndex ( PluginType type ) {
		return static_cast<size_t>(type);
	}

	//  通过抽象插件接口的IID获取插件类型
	std::optional<PluginType> getPluginType ( const QString& iid ) {
		if (iid == QStringLiteral ( FrameSourcePluginInterface_IID ))
			return PluginType::FrameSource;
		if (iid == QStringLiteral ( MemoryMonitorPluginInterface_IID ))
			return PluginType::MemoryMonitor;
		return std::nullopt;
	}

	// 是否加载 ： 插件启用且存在
	bool loadEnabled ( const PluginConfig& cfg ) {
		return cfg.enabled && cfg.exists;
	}
	bool loadEnabled ( const QVector<PluginConfig>& configs, PluginType type, QStringView pluginId ) {
		for (const PluginConfig& cfg : configs) {
			if (loadEnabled ( cfg ) && cfg.type == type && cfg.pluginId == pluginId.toString ())
				return true;
		}
		return false;
	}
	bool pluginTypeEnabled ( const QVector<PluginConfig>& configs, PluginType type ) {
		for (const PluginConfig& cfg : configs) {
			if (loadEnabled ( cfg ) && cfg.type == type)
				return true;
		}
		return false;
	}
}

PluginManager* PluginManager::instance () {
	if (!m_instance)
		Logger::instance ()->fatal ( QStringLiteral ( "[PluginManager]：未初始化，必须先调用 initialize()!" ) );
	return m_instance.get ();
}

PluginManager::~PluginManager () = default;

PluginManager* PluginManager::initialize ( const QStringList& pluginPaths,
	const QVector<PluginConfig>& pluginConfigs ) {
	if (!m_instance)
	{
		m_instance = PluginManagerPtr ( new PluginManager ( pluginPaths, pluginConfigs ) );
	}
	else
	{
		Logger::instance ()->warning ( QStringLiteral ( "[PluginManager]：PluginManager已初始化，重复调用initialize()" ) );
	}
	return m_instance.get ();
}

PluginManager::PluginManager ( const QStringList& pluginPaths,
	const QVector<PluginConfig>& pluginConfigs ) {
	pluginsMap.resize ( static_cast<int>(PluginCount) );

	for (const PluginConfig& cfg : pluginConfigs) {
		if (!cfg.enabled)
			Logger::instance ()->info ( QStringLiteral ( "[PluginManager]：插件已禁用，跳过 -> %1/%2" ).arg ( pluginTypeToString ( cfg.type ), cfg.pluginId ) );
		else if (!cfg.exists)
			Logger::instance ()->warning ( QStringLiteral ( "[PluginManager]：插件配置不存在，跳过 -> %1/%2" ).arg ( pluginTypeToString ( cfg.type ), cfg.pluginId ) );
	}

	for (const QString& path : pluginPaths)
	{
		LoaderPtr loader = std::make_unique<QPluginLoader> ( path );

		// 通过IID判断是否加载
		const auto type = getPluginType (
			loader->metaData ().value ( QStringLiteral ( "IID" ) ).toString () );
		if (!type)
		{
			Logger::instance ()->warning ( QStringLiteral ( "[PluginManager]：不是可识别的插件 -> " ) + path );
			continue;
		}
		if (!pluginTypeEnabled ( pluginConfigs, *type ))
		{
			Logger::instance ()->info ( QStringLiteral ( "[PluginManager]：插件类型未启用，跳过 -> " ) + path );
			continue;
		}

		if (!loader->load ())
		{
			Logger::instance ()->warning ( QStringLiteral ( "[PluginManager]：插件加载失败 -> " ) + path
				+ QStringLiteral ( " : " ) + loader->errorString () );
			continue;
		}

		// 加载插件
		QObject* instance = loader->instance ();
		auto* plugin = qobject_cast<PluginInterface*> ( instance );
		if (!plugin)
		{
			Logger::instance ()->warning ( QStringLiteral ( "[PluginManager]：插件未实现PluginInterface -> " ) + path );
			continue;
		}
		const QString pluginId = plugin->pluginId ();

		if (pluginId.trimmed ().isEmpty ())
		{
			Logger::instance ()->warning ( QStringLiteral ( "[PluginManager]：插件pluginId为空 -> " ) + path );
			continue;
		}

		if (!loadEnabled ( pluginConfigs, *type, pluginId ))
		{
			Logger::instance ()->info ( QStringLiteral ( "[PluginManager]：插件未启用或未标记存在，忽略 -> %1/%2" )
				.arg ( pluginTypeToString ( *type ), pluginId ) );
			continue;
		}

		QVariantHash& table = pluginsMap[static_cast<int>(getIndex ( *type ))];
		if (table.contains ( pluginId ))
		{
			Logger::instance ()->warning ( QStringLiteral ( "[PluginManager]：重复的插件ID -> " )
				+ pluginTypeToString ( *type ) + QStringLiteral ( "/" ) + pluginId );
			continue;
		}
		table.insert ( pluginId, QVariant::fromValue ( instance ) );
		pluginLoaders.push_back ( std::move ( loader ) );
		Logger::instance ()->info ( QStringLiteral ( "[PluginManager]：已加载%1插件 -> %2" )
			.arg ( pluginTypeToString ( *type ), pluginId ) );
	}

	// 配置启用但没有找到对应插件,输出警告
	for (const PluginConfig& cfg : pluginConfigs)
	{
		if (!loadEnabled ( cfg ))
			continue;
		if (!pluginExists ( cfg.type, cfg.pluginId ))
			Logger::instance ()->warning ( QStringLiteral ( "[PluginManager]：配置已启用但未找到插件 -> %1/%2" ).arg ( pluginTypeToString ( cfg.type ), cfg.pluginId ) );
	}
}

bool PluginManager::pluginExists ( PluginType type, QStringView pluginId ) const {
	const size_t index = getIndex ( type );
	if (index >= static_cast<size_t>(pluginsMap.size ()))
		return false;
	return pluginsMap.at ( static_cast<int>(index) ).contains ( pluginId );
}

bool PluginManager::pluginExists ( QStringView pluginId ) const {
	for (const QVariantHash& table : pluginsMap)
	{
		if (table.contains ( pluginId ))
			return true;
	}
	return false;
}

QStringList PluginManager::enumeratePluginIds () const {
	QStringList ids;
	for (const QVariantHash& table : pluginsMap)
	{
		for (auto it = table.constBegin (); it != table.constEnd (); ++it)
		{
			if (!ids.contains ( it.key () ))
				ids.append ( it.key () );
		}
	}
	return ids;
}

QStringList PluginManager::enumeratePluginIds ( PluginType type ) const {
	const size_t index = getIndex ( type );
	if (index >= static_cast<size_t>(pluginsMap.size ()))
		return QStringList ();
	return pluginsMap.at ( static_cast<int>(index) ).keys ();
}

FrameSourcePluginInterface* PluginManager::getFrameSourcePlugin ( QStringView pluginId ) const {
	return qobject_cast<FrameSourcePluginInterface*> (
		getPlugin ( PluginType::FrameSource, pluginId ).value<QObject*> () );
}

MemoryMonitorPluginInterface* PluginManager::getMemoryMonitorPlugin ( QStringView pluginId ) const {
	return qobject_cast<MemoryMonitorPluginInterface*> (
		getPlugin ( PluginType::MemoryMonitor, pluginId ).value<QObject*> () );
}

QVariant PluginManager::getPlugin ( PluginType type, QStringView pluginId ) const {
	const size_t index = getIndex ( type );
	if (index >= static_cast<size_t>(pluginsMap.size ()))
		return QVariant ();
	return pluginsMap.at ( static_cast<int>(index) ).value ( pluginId, QVariant () );
}

QVariant PluginManager::getPlugin ( QStringView pluginId ) const {
	for (const QVariantHash& table : pluginsMap)
	{
		if (table.contains ( pluginId ))
			return table.value ( pluginId );
	}
	return QVariant ();
}
