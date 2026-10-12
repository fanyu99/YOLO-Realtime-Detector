#pragma once
#include <QImage>
#include <QString>
#include <QVector>

#include <opencv2/opencv.hpp>

#include <span>

#include "Box.h"

// 把「帧 + 检测框」渲染成可以直接给 QML 的 QImage。
//
// 渲染放在 worker 线程。
// 数组参数一律用 std::span：调用方传 QVector / std::vector / 数组皆可，
// 只要内存连续；渲染是同步调用，span 不跨越调用期，无生命周期风险。
namespace DetectionRenderer {
				  
	// 只做颜色空间转换与内存所有权转移，不画框
	QImage toQImage(const cv::Mat& bgr);

	// 在原帧副本上画框 + 类别 + 左上角状态（帧率 / 耗时），返回 BGR 画面。
	// 保存视频时必须用这个版本（VideoWriter 吃 cv::Mat）；只想显示的话用 render()。
	// fps <= 0 时不画帧率；durationMs <= 0 时不画耗时。
	cv::Mat renderMat(const cv::Mat& frame,
					  std::span<const Box> boxes,
					  std::span<const QString> classNames = {},
					  long long durationMs = 0,
					  float fps = 0.0f);

	// renderMat() 的结果转成 QImage。显示路径用这个。
	// 配色与 ImageShower 保持一致，两条显示路径看起来一样。
	QImage render(const cv::Mat& frame,
				  std::span<const Box> boxes,
				  std::span<const QString> classNames = {},
				  long long durationMs = 0,
				  float fps = 0.0f);

}
