#include "DetectionRenderer.h"
 // 画框 + 画类别 + 画帧率+ 画推理延迟
namespace DetectionRenderer {

	namespace {
		// 左上角状态文字的统一样式。字号缩到 0.5 倍后笔画很细，
		constexpr double kStatusFontScale = 0.5;
		constexpr int    kStatusLineHeight = 18;
		constexpr int    kStatusLeft = 10;
		constexpr int    kStatusTop = 18;

		void putStatusText(cv::Mat& img, const std::string& text, int lineIndex) {
			const cv::Point org(kStatusLeft, kStatusTop + lineIndex * kStatusLineHeight);
			cv::putText(img, text, org, cv::FONT_HERSHEY_SIMPLEX, kStatusFontScale,
				cv::Scalar(0, 0, 0), 3, cv::LINE_AA);		// 描边
			cv::putText(img, text, org, cv::FONT_HERSHEY_SIMPLEX, kStatusFontScale,
				cv::Scalar(0, 255, 0), 1, cv::LINE_AA);		// 字身
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

	cv::Mat renderMat(const cv::Mat& frame, const QVector<Box>& boxes,
		const QVector<QString>& classNames, long long durationMs, float fps)
	{
		if (frame.empty())
			return cv::Mat();

		cv::Mat canvas = frame.clone();		// 不能改调用方那一帧

		// 左上角状态：帧率一行、耗时一行，都缩到 0.5 倍字号
		int line = 0;
		if (fps > 0.0f) {
			putStatusText(canvas,
				(QString::number(static_cast<double>(fps), 'f', 1)
					+ QStringLiteral(" fps")).toStdString(),
				line++);
		}
		if (durationMs > 0) {
			putStatusText(canvas,
				(QString::number(durationMs) + QStringLiteral(" ms")).toStdString(),
				line++);
		}

		const int classCount = static_cast<int>(classNames.size());
		for (const Box& box : boxes) {
			const cv::Rect rect = Box::Box2Rect(box, canvas.cols, canvas.rows);
			if (rect.width <= 0 || rect.height <= 0)
				continue;					// 越界框直接跳过，别画到画布外

			cv::rectangle(canvas, rect, cv::Scalar(255, 0, 0), 2);

			if (box.cls >= 0 && box.cls < classCount) {
				const QString label = classNames[box.cls] + QStringLiteral(" ")
					+ QString::number(box.conf, 'f', 6);
				cv::putText(canvas, label.toStdString(), rect.tl(),
					cv::FONT_HERSHEY_COMPLEX_SMALL, 1, cv::Scalar(255, 255, 255), 2);
			}
		}

		return canvas;
	}

	QImage render(const cv::Mat& frame, const QVector<Box>& boxes,
		const QVector<QString>& classNames, long long durationMs, float fps)
	{
		return toQImage(renderMat(frame, boxes, classNames, durationMs, fps));
	}

}
