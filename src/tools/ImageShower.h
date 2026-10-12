#pragma once
#include <opencv2/opencv.hpp>
#include <QString>
#include <QVector>
#include <span>
#include "Box.h"
// 显示图片和视频工具
// boxes / class_names 用 std::span：只读借用调用方的连续内存，避免容器适配拷贝。
class ImageShower {
public :
	static void showImage(const QString& imagePath); // 显示图片(路径)
	static void showImage(const cv::Mat& image); // 显示图片(cv::Mat)
	static void showDetections(const cv::Mat& image, std::span<const Box> boxes, long long duration, std::span<const QString> class_names = {}); // 显示带检测框的图片	(duration为毫秒)
	static void showDetections(const cv::Mat& image, std::span<const Box> boxes, std::span<const QString> class_names = {});
};
class VideoShower : public ImageShower {
public:
	static void showVideo(const QString& videoPath); // 显示视频(路径)
	static void showCamera(int cameraIndex = 0); // 显示摄像头
};
