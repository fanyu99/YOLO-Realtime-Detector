#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QPalette>
#include <QStyleHints>
#include <qDebug>
#include "Logger.h"

// 深色配色：深灰背景 + 高对比度白色文字
QPalette makeDarkPalette() {
	QPalette p;
	const QColor text(0xf2, 0xf2, 0xf2);		// 主文字：接近纯白，对比度高
	const QColor window(0x1e, 0x1e, 0x1e);		// 窗口背景
	const QColor panel(0x2a, 0x2a, 0x2b);		// 面板 / 按钮背景
	const QColor base(0x16, 0x16, 0x17);		// 输入框 / 显示区背景
	const QColor accent(0x3b, 0x8e, 0xe0);		// 强调色
	const QColor disabled(0x7a, 0x7a, 0x7a);

	p.setColor(QPalette::Window, window);
	p.setColor(QPalette::WindowText, text);
	p.setColor(QPalette::Base, base);
	p.setColor(QPalette::AlternateBase, panel);
	p.setColor(QPalette::Text, text);
	p.setColor(QPalette::Button, panel);
	p.setColor(QPalette::ButtonText, text);
	p.setColor(QPalette::BrightText, QColor(0xff, 0x6b, 0x6b));
	p.setColor(QPalette::ToolTipBase, panel);
	p.setColor(QPalette::ToolTipText, text);
	p.setColor(QPalette::PlaceholderText, QColor(0x9a, 0x9a, 0x9a));
	p.setColor(QPalette::Mid, QColor(0x3d, 0x3d, 0x3d));		// 边框
	p.setColor(QPalette::Midlight, QColor(0x50, 0x50, 0x50));
	p.setColor(QPalette::Dark, QColor(0x14, 0x14, 0x14));
	p.setColor(QPalette::Light, QColor(0x55, 0x55, 0x55));
	p.setColor(QPalette::Shadow, QColor(0x0a, 0x0a, 0x0a));
	p.setColor(QPalette::Highlight, accent);
	p.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
	p.setColor(QPalette::Link, accent);
	// 禁用态文字也要看得清
	p.setColor(QPalette::Disabled, QPalette::Text, disabled);
	p.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
	p.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
	return p;
}

// 浅色配色：白底 + 深色文字
QPalette makeLightPalette() {
	QPalette p;
	const QColor text(0x1a, 0x1a, 0x1a);
	const QColor window(0xf4, 0xf4, 0xf4);
	const QColor panel(0xff, 0xff, 0xff);
	const QColor base(0xff, 0xff, 0xff);
	const QColor accent(0x0a, 0x66, 0xc2);

	p.setColor(QPalette::Window, window);
	p.setColor(QPalette::WindowText, text);
	p.setColor(QPalette::Base, base);
	p.setColor(QPalette::AlternateBase, QColor(0xec, 0xec, 0xec));
	p.setColor(QPalette::Text, text);
	p.setColor(QPalette::Button, QColor(0xe8, 0xe8, 0xe8));
	p.setColor(QPalette::ButtonText, text);
	p.setColor(QPalette::BrightText, QColor(0xc0, 0x26, 0x26));
	p.setColor(QPalette::ToolTipBase, panel);
	p.setColor(QPalette::ToolTipText, text);
	p.setColor(QPalette::PlaceholderText, QColor(0x8a, 0x8a, 0x8a));
	p.setColor(QPalette::Mid, QColor(0xc9, 0xc9, 0xc9));
	p.setColor(QPalette::Midlight, QColor(0xdd, 0xdd, 0xdd));
	p.setColor(QPalette::Dark, QColor(0xb0, 0xb0, 0xb0));
	p.setColor(QPalette::Light, QColor(0xff, 0xff, 0xff));
	p.setColor(QPalette::Shadow, QColor(0x90, 0x90, 0x90));
	p.setColor(QPalette::Highlight, accent);
	p.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
	p.setColor(QPalette::Link, accent);
	return p;
}

int main(int argc, char* argv[])
{
#if defined(Q_OS_WIN) && QT_VERSION_CHECK(5, 6, 0) <= QT_VERSION && QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
	QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif
	// 统一控件风格为 Fusion：它会完整遵循下面的调色板，
	// 这样自定义主题与 Button/SpinBox/TextArea 等控件外观才能保持一致
	qputenv("QT_QUICK_CONTROLS_STYLE", "Fusion");

	QGuiApplication app(argc, argv);
	// 尽早创建日志器单例：接管 qDebug/qInfo/qWarning 等输出，保证启动阶段的日志也能进入 QML 日志面板
	Logger::instance();

	// 依据系统主题应用调色板，并跟随系统主题变化实时切换
	auto applyTheme = [](Qt::ColorScheme scheme) {
		qApp->setPalette(scheme == Qt::ColorScheme::Dark ? makeDarkPalette() : makeLightPalette());
	};
	applyTheme(QGuiApplication::styleHints()->colorScheme());
	QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, qApp, applyTheme);

	try {
		QQmlApplicationEngine engine;
		engine.load(QUrl(QStringLiteral("qrc:/qt/qml/yolo/main.qml")));
		if (engine.rootObjects().isEmpty())
			return -1;
		return app.exec();
	}
	catch (const std::exception& e) {
		qDebug() << "标准异常:" << e.what();
	}
	catch (...) {
		qDebug() << "未知异常";
	}
	return app.exec();
}
