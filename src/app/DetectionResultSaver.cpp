#include "DetectionResultSaver.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>

#include "DetectionRenderer.h"
#include "DetectionTypes.h"

namespace {
	constexpr const char* kResultsSubDir = "detection_results";
	constexpr double kFallbackFps = 25.0;		// 摄像头取不到帧率时用

	bool exists(const QDir& dir, const QString& stem) {
		return QFileInfo::exists(dir.filePath(stem + QStringLiteral(".jpg")))
			|| QFileInfo::exists(dir.filePath(stem + QStringLiteral(".mp4")));
	}

	// 同名文件已存在时加个后缀，避免静默覆盖用户上一次的结果
	QString uniqueBaseName(const QDir& dir, const QString& stem) {
		if (!exists(dir, stem))
			return stem;
		for (int i = 1; i < 1000; ++i) {
			const QString candidate = stem + QStringLiteral("_") + QString::number(i);
			if (!exists(dir, candidate))
				return candidate;
		}
		return stem;
	}
}

QString DetectionResultSaver::resultsDirFor(const QString& rootDir) {
	if (rootDir.isEmpty())
		return QString();
	return QDir(rootDir).filePath(QString::fromLatin1(kResultsSubDir));
}

bool DetectionResultSaver::begin(const DetectionTask& task, int width, int height, double fps) {
	m_active = false;
	m_isVideo = false;
	m_written = 0;
	m_error.clear();
	m_dir.clear();
	m_imagePath.clear();

	if (!task.shared || task.shared->saveDir.isEmpty())
		return false;						// 没开启保存：静默跳过，lastError 保持为空

	// saveDir 已经是由 resultsDirFor() 组合好的最终目录（约定只在一处定义）
	m_dir = task.shared->saveDir;
	if (!QDir().mkpath(m_dir)) {
		m_error = QStringLiteral("无法创建保存目录: ") + m_dir;
		return false;
	}

	const QDir dir(m_dir);
	QString stem = QFileInfo(task.source).completeBaseName();
	if (stem.isEmpty())						// 摄像头任务没有源文件名
		stem = QStringLiteral("task_") + QString::number(task.taskId);
	stem = uniqueBaseName(dir, stem);

	m_isVideo = (task.type == DetectionTaskType::VideoStream);

	if (m_isVideo) {
		const double useFps = fps > 1.0 ? fps : kFallbackFps;
		const QString path = dir.filePath(stem + QStringLiteral(".mp4"));
		// mp4v 在 FFMPEG 后端上通用性最好
		const int fourcc = cv::VideoWriter::fourcc('m', 'p', '4', 'v');
		if (!m_writer.open(path.toStdString(), fourcc, useFps, cv::Size(width, height))) {
			m_error = QStringLiteral("无法创建视频文件: ") + path;
			return false;
		}
	}
	else {
		m_imagePath = dir.filePath(stem + QStringLiteral(".jpg"));
	}

	m_active = true;
	return true;
}

bool DetectionResultSaver::write(const cv::Mat& bgrFrame) {
	if (!m_active || bgrFrame.empty())
		return false;

	if (m_isVideo) {
		m_writer.write(bgrFrame);
	}
	else {
		// 用 QImage::save 而不是 cv::imwrite：前者支持非 ASCII 路径，
		// 而用户自选的保存目录里带中文是很常见的
		if (!DetectionRenderer::toQImage(bgrFrame).save(m_imagePath)) {
			m_error = QStringLiteral("写入图片失败: ") + m_imagePath;
			return false;
		}
	}

	++m_written;
	return true;
}

void DetectionResultSaver::end() {
	if (m_writer.isOpened())
		m_writer.release();
	m_active = false;
}
