#include <QtTest/QTest>
#include <QElapsedTimer>
#include <filesystem>
#include <string>
#include <vector>
#include <cstdlib>

#include "Detector.h"
#include "Logger.h"

constexpr int num_boxes = 10000; // 测试用的目标框数量
constexpr char videoRootPath[] = "D:\\测试视频";
constexpr char modelPath[] = "D:\\python\\YOLO\\runs\\train\\people_yolo26n_train_e100s1280\\weights\\best.onnx";
// 获取视频文件路径
std::vector<std::string> getVideoFiles( const std::string& rootPath ) {
	std::vector<std::string> videoFiles;
	// 遍历目录，获取所有视频文件
	for (const auto& entry : std::filesystem::recursive_directory_iterator( rootPath )) {
		if (entry.is_regular_file()) {
			const std::string& filePath = entry.path().string();
			// 检查文件扩展名是否为常见的视频格式
			if (filePath.ends_with( ".mp4" ) || filePath.ends_with( ".avi" ) || filePath.ends_with( ".mkv" )) {
				videoFiles.push_back( filePath );
			}
		}
	}
	return videoFiles;
}
// 生成大量的BOX
std::vector<Box> generateBoxes( int num_boxes ) {
	std::vector<Box> boxes;
	boxes.reserve( static_cast<size_t>(num_boxes) );
	for (int i = 0; i < num_boxes; ++i) {
		float x = static_cast<float>( rand() % 640 );
		float y = static_cast<float>( rand() % 640 );
		float w = static_cast<float>( rand() % 100 + 30 ); // 宽度在20到120之间
		float h = static_cast<float>( rand() % 100 + 30 ); // 高度在20到120之间
		float conf = static_cast<float>( rand() ) / RAND_MAX; // 置信度在0到1之间
		int cls = 0; 
		boxes.push_back( Box{ x, y, w, h, conf, cls } );
	}
	return boxes;
}
class nms_test : public QObject {
	Q_OBJECT

private slots:
	// 测试大量框下NMS的耗时
	void test_performance() {
		config.input_width = 1280;
		config.input_height = 1280;
		config.class_names = {"person"};
		config.conf_threshold = 0.4f;
		config.use_cuda = true;
		config.iou_threshold = 0.5f;
		config.model_path = std::filesystem::path(modelPath);
		m_detector.setConfig(config);
		Logger::instance()->setLevel(Logger::Level::Debug);
		std::vector<Box>boxes = generateBoxes(num_boxes); // 获取大量Box
		QElapsedTimer timer;
		timer.start();
		std::vector<Box>result = m_detector.NMS( boxes, config.iou_threshold );
		quint64 ms = timer.elapsed();
		Logger::instance()->debug(QString::number(num_boxes) 
		+ QStringLiteral("个置信框，耗时： ")+QString::number(ms)+QStringLiteral(" ms，平均：")+QString::number(ms/num_boxes)+QStringLiteral(" ms"));
		}

private:
	Detector m_detector;
	DetectorConfig config;
	std::vector<std::string>filePaths;
};
QTEST_MAIN(nms_test)
#include "test_duration_nms.moc"