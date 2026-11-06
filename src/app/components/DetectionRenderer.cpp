#include "DetectionRenderer.h"
#include <algorithm>
#include <cmath>
#include <span>
 // 画框 + 画类别 + 画帧率+ 画推理延迟
namespace DetectionRenderer {

	namespace {
		// 左上角状态文字的统一样式(以短边 1080 为基准，实际尺寸按分辨率放大)
		constexpr double kStatusFontScale = 1.0;
		constexpr int    kStatusLineHeight = 18;
		constexpr int    kStatusLeft = 10;
		constexpr int    kStatusTop = 18;

		// 基准分辨率：短边 1080 时上述常量按原值使用
		constexpr double kReferenceShortSide = 1080.0;
		// 缩放限幅：过小的图不把字缩到看不清，过大的图也不无限放大
		constexpr double kMinUiScale = 0.75;
		constexpr double kMaxUiScale = 8.0;

		// 按画面短边推算一个整体缩放系数：
		// 720p -> 0.67(会被夹到 0.75)，1080p -> 1.0，4K -> 2.0，20MP 工业相机 -> 约 3.7
		double uiScale(const cv::Mat& img) {
			const int shortSide = std::min(img.cols, img.rows);
			const double s = static_cast<double>(shortSide) / kReferenceShortSide;
			return std::clamp(s, kMinUiScale, kMaxUiScale);
		}

		// 线宽至少 1 像素，并按缩放取整
		int scaledThickness(int base, double scale, int minimum = 1) {
			return std::max(minimum, static_cast<int>(std::lround(base * scale)));
		}

		void putStatusText(cv::Mat& img, const std::string& text, int lineIndex, double scale) {
			const int left = scaledThickness(kStatusLeft, scale);
			const int top = scaledThickness(kStatusTop, scale);
			const int lineHeight = scaledThickness(kStatusLineHeight, scale);
			const cv::Point org(left, top + lineIndex * lineHeight);
			cv::putText(img, text, org, cv::FONT_HERSHEY_SIMPLEX, kStatusFontScale * scale,
				cv::Scalar(0, 0, 0), scaledThickness(3, scale), cv::LINE_AA);		// 描边
			cv::putText(img, text, org, cv::FONT_HERSHEY_SIMPLEX, kStatusFontScale * scale,
				cv::Scalar(0, 255, 0), scaledThickness(1, scale), cv::LINE_AA);		// 字身
		}
	}

	QImage toQImage(const cv::Mat& bgr) {
		if (bgr.empty())
			return QImage();

		cv::Mat rgb;
		switch (bgr.channels()) {
		case 3:  cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);  break;
		case 4:  cv::cvtColor(bgr, rgb, cv::COLOR_BGRA2RGB); break;
		default: cv::cvtColor(bgr, rgb, cv::COLOR_GRAY2RGB); break;
		}

		// copy() 让 QImage 自己持有一份像素。
		return QImage(rgb.data, rgb.cols, rgb.rows,
			static_cast<int>(rgb.step), QImage::Format_RGB888).copy();
	}

	cv::Mat renderMat(const cv::Mat& frame, std::span<const Box> boxes,
		std::span<const QString> classNames, long long durationMs, float fps)
	{
		if (frame.empty())
			return cv::Mat();

		cv::Mat canvas = frame.clone();		// 不能改调用方那一帧

		// 所有文字/线宽都按画布短边等比放大，高分辨率下才不会缩成看不清的小字
		const double scale = uiScale(canvas);

		// 左上角状态：帧率一行、耗时一行，都缩到 0.5 倍字号
		int line = 0;
		if (fps > 0.0f) {
			putStatusText(canvas,
				(QString::number(static_cast<double>(fps), 'f', 1)
					+ QStringLiteral(" fps")).toStdString(),
				line++, scale);
		}
		if (durationMs > 0) {
			putStatusText(canvas,
				(QString::number(durationMs) + QStringLiteral(" ms")).toStdString(),
				line++, scale);
		}

		// 检测框与类别标签同样随分辨率缩放
		const double labelFontScale = std::clamp(1.0 * scale, 1.0, kMaxUiScale);
		const int boxThickness = scaledThickness(2, scale);

		const int classCount = static_cast<int>(classNames.size());
		for (const Box& box : boxes) {
			const cv::Rect rect = Box::Box2Rect(box, canvas.cols, canvas.rows);
			if (rect.width <= 0 || rect.height <= 0)
				continue;					// 越界框直接跳过，别画到画布外

			cv::rectangle(canvas, rect, cv::Scalar(255, 0, 0), boxThickness);

			if (box.cls >= 0 && box.cls < classCount) {
				const QString label = classNames[box.cls] + QStringLiteral(" ")
					+ QString::number(box.conf, 'f', 6);
				cv::putText(canvas, label.toStdString(), rect.tl(),
					cv::FONT_HERSHEY_COMPLEX_SMALL, labelFontScale, cv::Scalar(255, 255, 255),
					boxThickness);
			}
		}

		return canvas;
	}

	QImage render(const cv::Mat& frame, std::span<const Box> boxes,
		std::span<const QString> classNames, long long durationMs, float fps)
	{
		return toQImage(renderMat(frame, boxes, classNames, durationMs, fps));
	}

}
