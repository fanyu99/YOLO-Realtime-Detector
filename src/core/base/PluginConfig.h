#pragma once
#include <cstddef>
#include <optional>
#include <QString>
#include <QStringView>

// 插件类型
enum class PluginType {
	FrameSource,	   // 帧源插件
	MemoryMonitor,   // 内存监控插件
	Count			   // 数量
};
constexpr size_t PluginCount = static_cast<size_t>(PluginType::Count);

inline QString pluginTypeToString ( PluginType type ) {
	switch (type) {
	case PluginType::FrameSource: return QStringLiteral ( "FrameSource" );
	case PluginType::MemoryMonitor: return QStringLiteral ( "MemoryMonitor" );
	default: return QStringLiteral ( "Unknown" );
	}
}

// 文本 -> 插件类型，无法识别时返回 std::nullopt
inline std::optional<PluginType> pluginTypeFromString ( QStringView text ) {
	if (text.compare ( QStringLiteral ( "FrameSource" ), Qt::CaseInsensitive ) == 0)
		return PluginType::FrameSource;
	if (text.compare ( QStringLiteral ( "MemoryMonitor" ), Qt::CaseInsensitive ) == 0)
		return PluginType::MemoryMonitor;
	return std::nullopt;
}

// 单个插件的配置，对应 app.json 中 plugins 数组的一项
struct PluginConfig {
	PluginType type{PluginType::FrameSource};
	QString pluginId;
	bool enabled{true};	   // 用户是否启用该插件
	bool exists{false};	   // 上次启动时是否发现该插件(运行期以 PluginManager 为准)
};
