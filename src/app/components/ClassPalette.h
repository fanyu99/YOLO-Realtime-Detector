#pragma once
#include <QColor>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// 检测框配色
namespace ClassPalette {
	inline constexpr int kHueStep = 43; // 设置为质数
	inline constexpr int kSaturation = 220;
	inline constexpr int kValue = 255;

	// 推导类别对应的颜色
	inline QColor getClassColor ( int cls ) {
		const int hue = ( ( cls * kHueStep ) % 360 + 360 ) % 360;
		return QColor::fromHsv ( hue, kSaturation, kValue );
	}

	// 获取类别和颜色的列表
	// 类别号 + 类别名 + 颜色(十六进制)
	inline QVariantList cls_colorInfo ( const QStringList& classNames ) {
		QVariantList info;
		info.reserve ( classNames.size () );
		for ( int cls = 0; cls < classNames.size (); ++cls ) {
			QVariantMap item;
			item.insert ( QStringLiteral ( "cls" ), cls );
			item.insert ( QStringLiteral ( "name" ), classNames.at ( cls ) );
			item.insert ( QStringLiteral ( "color" ), getClassColor ( cls ).name ( QColor::HexRgb ) );
			info.append ( item );
		}
		return info;
	}
}
