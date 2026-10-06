#include "ImageShower.h"
#include <QDebug>
// 显示图片
void ImageShower::showImage(const QString& imagePath) {
	cv::Mat image = cv::imread(imagePath.toStdString());
	if (image.empty()) {
		qWarning() << "图片为空/加载失败!";
		return;
	}
	cv::imshow("图片", image);
}

void ImageShower::showImage(const cv::Mat& image) {
	if (image.empty()) {
		qWarning() << "图片为空/加载失败!";
		return;
	}
	cv::imshow("图片", image);
}
// 显示带检测框的图片(duration为毫秒)
void ImageShower::showDetections(const cv::Mat& imag, const QVector<Box>& boxes,  long long duration ,const QVector<QString>& class_names) {

	cv::Mat image = imag.clone();
	if (image.empty()) {
		qWarning() << "图片为空/加载失败!";
		return;
	}
	if (duration > 0)
		cv::putText(image, (QString::number(duration) + QStringLiteral("ms")).toStdString(), cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 255, 0), 2);
	else qWarning() << "无效延迟!";
	for (const auto& box : boxes) {
		cv::Rect rect = Box::Box2Rect(box, image.cols, image.rows);
		cv::rectangle(image, rect, cv::Scalar(255, 0, 0), 2);
		if (!class_names.isEmpty() && box.cls >= 0 && box.cls < class_names.size()) {
			const QString label = class_names[box.cls] + u' ' + QString::number(box.conf, 'f', 6);
			cv::putText(image, label.toStdString(), rect.tl(), cv::FONT_HERSHEY_COMPLEX_SMALL, 1, cv::Scalar(255, 255, 255), 2);
		}
		else
			qWarning() << "类别名称为空或索引越界!";
	}
	cv::imshow("检测结果", image);
}
// 显示带检测框的图片
void ImageShower::showDetections(const cv::Mat& imag, const QVector<Box>& boxes, const QVector<QString>& class_names) {
	cv::Mat image = imag.clone();
	if (image.empty()) {
		qWarning() << "图片为空/加载失败!";
		return;
	}
	for (const auto& box : boxes) {
		cv::Rect rect = Box::Box2Rect(box, image.cols, image.rows);
		cv::rectangle(image, rect, cv::Scalar(255, 0, 0), 2);
		if (!class_names.isEmpty() && box.cls >= 0 && box.cls < class_names.size()) {
			const QString label = class_names[box.cls] + u' ' + QString::number(box.conf, 'f', 6);
			cv::putText(image, label.toStdString(), rect.tl(), cv::FONT_HERSHEY_COMPLEX_SMALL, 1, cv::Scalar(255, 255, 255), 2);
		}
		else
			qWarning() << "类别名称为空或索引越界!";
	}
	cv::imshow("检测结果", image);
}

void VideoShower::showVideo(const QString& videoPath) {
	cv::VideoCapture cap(videoPath.toStdString());
	if (!cap.isOpened()) {
		qWarning() << "视频打开失败!";
		return;
	}
	cv::Mat frame;
	while (true) {
		cap >> frame;
		if (frame.empty()) break;
		cv::imshow("视频", frame);
		if (cv::waitKey(30)== 27) break; // 按ESC键退出
	}
}

void VideoShower::showCamera(int cameraIndex) {
	cv::VideoCapture cap(cameraIndex);
	if (!cap.isOpened()) {
		qWarning() << "摄像头打开失败!";
		return;
	}
	cv::Mat frame;
	while (true) {
		cap >> frame;
		if (frame.empty()) break;
		cv::imshow("摄像头", frame);
		if (cv::waitKey(30) == 27) break; // 按ESC键退出
	}
}
