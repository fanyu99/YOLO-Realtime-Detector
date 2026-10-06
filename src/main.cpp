#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QPalette>
#include <QStyleHints>
#include <qDebug>
#include "Detector.h"
#include "Box.h"
#include "ImageShower.h"
#include "Logger.h"
 const QVector<QString> AfricanWildlife_class_names = {
	QStringLiteral("buffalo"),
	QStringLiteral("elephant"),
	QStringLiteral("rhino")	,
	QStringLiteral("zebra")
};
 const QVector<QString>People_class_names = {
	QStringLiteral("Person"),
 };
auto AfricanWildlife_model_path = L"D:\\python\\YOLO\\runs\\train\\african_wildlife_yolo26n_train_e50s640\\weights\\best.onnx";
auto People_model_path = L"D:\\python\\YOLO\\runs\\train\\people_yolo26n_train_e100s1280\\weights\\best.onnx";
 const QString image_path = R"(D:\python\datasets\african-wildlife\images\train\2 (50).jpg)";

 // 检测图片
 void detect_image(const QString& image_path, const DetectorConfig& config) {
	 ImageShower imageShower;
	 Detector detector(config);
	 DetectionResult result = detector.detect(image_path);
	 cv::Mat image = cv::imread(image_path.toStdString());
	 if (image.empty()) {
		 qWarning() << "图片加载失败!";
		 return;
	 }
	 imageShower.showDetections(image, result.boxes, result.duration, config.class_names);
	 cv::waitKey(0);
 }
 // 检测视频
 void detect_video(const QString& video_path, const DetectorConfig& config) {
	 ImageShower imageShower;
	 Detector detector(config);
	 cv::VideoCapture cap(video_path.toStdString());
	 if (!cap.isOpened()) {
		 qWarning() << "打开视频失败:" << video_path;
		 return;
	 }
	 cv::Mat frame;
	 while (true) {
		 cap >> frame;
		 if (frame.empty()) break;
		 DetectionResult result = detector.detect(frame);
		 imageShower.showDetections(frame, result.boxes, result.duration, config.class_names);
		 if (cv::waitKey(1) == 27)
		 {
			 cv::destroyAllWindows();
			 break;
		 }
	 }
 }

 // 检测摄像头
 void detect_camera(size_t camera_id,const DetectorConfig& config) {
	 ImageShower imageShower;
	 Detector detector(config);
	 cv::VideoCapture cap(camera_id);
	 if (!cap.isOpened()) {
		 qWarning() << "打开摄像头失败:" << camera_id;
		 return;
	 }
	 cv::Mat frame;
	 while (true) {
		 cap >> frame;
		 if(frame.empty()) break;  
		 DetectionResult result = detector.detect(frame);
		 imageShower.showDetections(frame, result.boxes,result.duration,config.class_names);
		 if (cv::waitKey(1) == 27)
		 {
			 cv::destroyAllWindows();
			 break;
		 }
	 }
 }

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
		DetectorConfig AfricanWildlife_config;
		AfricanWildlife_config.class_names =AfricanWildlife_class_names;
		AfricanWildlife_config.conf_threshold = 0.2f;
		AfricanWildlife_config.iou_threshold = 0.7f;
		AfricanWildlife_config.model_path = std::filesystem::path(AfricanWildlife_model_path);
		AfricanWildlife_config.num_threads = 0;
		AfricanWildlife_config.input_height = 640;
		AfricanWildlife_config.input_width = 640;

		DetectorConfig People_config;
		People_config.class_names = People_class_names;
		People_config.conf_threshold = 0.2f;
		People_config.iou_threshold = 0.7f;
		People_config.model_path = std::filesystem::path(People_model_path);
		People_config.num_threads = 0;
		People_config.input_height = 1280;
		People_config.input_width = 1280;

		// 启动时的示例检测会阻塞在 cv::waitKey(0) 上，导致 QML 窗口迟迟不显示；
		// 等推理接入 UI 后再启用，这里先注释掉。
		//detect_image(image_path,AfricanWildlife_config);


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
