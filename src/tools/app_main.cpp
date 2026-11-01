#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include "RtspImageProvider.h"
#include "RtspStreamController.h"

int main(int argc, char* argv[]) {
	QGuiApplication app(argc, argv);

	QQmlApplicationEngine engine;

	// controller 声明在 engine 之后：退出时先析构 controller（停掉解码线程），
	// 再由 engine 释放 image provider，避免关窗瞬间还有帧回调。
	RtspStreamController controller;

	// 路径 B：provider 由 C++ 侧注册，所有权交给 engine。
	// 必须在 engine.load() 之前注册，QML 才能解析 image://rtsp/...
	auto* imageProvider = new RtspImageProvider;
	engine.addImageProvider(QStringLiteral("rtsp"), imageProvider);

	QObject::connect(&controller, &RtspStreamController::FrameUpdated,
		imageProvider, [imageProvider](const LastestFrameStore& frame) {
			imageProvider->updateFrame(frame);
		});

	// 终态清空缓存帧，避免下一次启动前 provider 里还留着上一路流的画面
	QObject::connect(&controller, &RtspStreamController::StatusChanged,
		imageProvider, [imageProvider](RtspStatus status) {
			if (status == RtspStatus::Idle || status == RtspStatus::Stopped || status == RtspStatus::Error)
				imageProvider->clear();
		});

	engine.rootContext()->setContextProperty(QStringLiteral("rtspController"), &controller);
	engine.load(QUrl(QStringLiteral("qrc:/qt/qml/RtspPuller/main.qml")));
	if (engine.rootObjects().isEmpty())
		return -1;

	return app.exec();
}
