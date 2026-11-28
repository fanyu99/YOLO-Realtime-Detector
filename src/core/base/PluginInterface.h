#pragma once
#include <QtPlugin>
#include <QString>

// 所有插件接口的抽象接口
class PluginInterface {
public:
	virtual ~PluginInterface () = default;
	virtual QString pluginId () const = 0;
};

#define PluginInterface_IID "com.yoloqt.PluginInterface"
Q_DECLARE_INTERFACE ( PluginInterface, PluginInterface_IID )
