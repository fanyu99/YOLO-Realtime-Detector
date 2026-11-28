#pragma once
#include <QString>
#include <QStringList>
#include <QStringView>
#include <QVector>
#include "FrameSource.h"
#include "FrameSourcePluginInterface.h"
#include "MemoryMonitorPluginInterface.h"
#include "PluginConfig.h"
#include <QVariant>
#include <QVariantHash>
#include <QPluginLoader>
#include <memory>
#include <vector>
using LoaderPtr = std::unique_ptr<QPluginLoader>;
// 插件管理器
// 按 app.json 的 plugins 配置选择加载
class PluginManager {
public:
using PluginManagerPtr = std::unique_ptr<PluginManager>;
	PluginManager ( const PluginManager& ) = delete;
	PluginManager& operator= ( const PluginManager& ) = delete;
	~PluginManager ();
	// 初始化插件并返回单例，必须先调用 initialize() 初始化插件路径后才能调用 instance()
	static PluginManager* initialize ( const QStringList& pluginPaths,
		const QVector<PluginConfig>& pluginConfigs );
	static PluginManager* instance ();

	// 指定类型和pluginId的插件是否已加载
	bool pluginExists ( PluginType type, QStringView pluginId ) const;
	bool pluginExists ( QStringView pluginId ) const;

	// 已加载插件ID列表
	QStringList enumeratePluginIds () const;
	QStringList enumeratePluginIds ( PluginType type ) const;

	// 按类型取插件 不存在返回nullptr
	FrameSourcePluginInterface* getFrameSourcePlugin ( QStringView pluginId ) const;
	MemoryMonitorPluginInterface* getMemoryMonitorPlugin ( QStringView pluginId ) const;

	// 通过类型和pluginId查找插件（借用指针）
	// 返回QVariant,内部存放插件的 QObject* ，需要自行转换为插件指针
	// QVariant.isNull()表示插件不存在
	QVariant getPlugin ( PluginType type, QStringView pluginId ) const;
	QVariant getPlugin ( QStringView pluginId ) const;
private:
	explicit PluginManager ( const QStringList& pluginPaths,
		const QVector<PluginConfig>& pluginConfigs );
	// 插件表,按照PluginType下标访问
	// PluginType对应的插件表是 QHash<pluginId,  QObject*>
	QVector<QVariantHash> pluginsMap;
	// 插件加载器(unique_ptr，使用std::vector，不使用QVector)
	std::vector<LoaderPtr> pluginLoaders;

	inline static PluginManagerPtr m_instance{nullptr}; // 单例
};
