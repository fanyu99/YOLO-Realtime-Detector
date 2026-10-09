#pragma once
#include <QString>

#include <opencv2/opencv.hpp>

struct DetectionTask;

// 把一个 Task 的渲染结果保存到文件夹中。目录约定：
//
//   <用户选的根目录>/detection_results/
//       图片任务 -> <原文件名>.jpg
//       视频/摄像头任务 -> <原文件名>.mp4（VideoWriter 顺序写）
//
// 只保存"画了框的画面"
// 
// 错误记录在lastError()中

class DetectionResultSaver {
public:
	static QString resultsDirFor(const QString& rootDir);

	// 开始一个 Task。
	//   - task.shared->saveDir 为空 -> 不做任何事，active() 为 false（静默跳过）
	//   - 返回 false 且 lastError() 非空 -> 真的失败了，调用方应上报
	// width/height/fps 只在视频任务里用得上（VideoWriter 需要）。
	bool begin(const DetectionTask& task, int width, int height, double fps);

	bool write(const cv::Mat& bgrFrame);		// 写一帧；图片任务只会调一次
	void end();									// 收尾（释放 VideoWriter）

	bool active() const { return m_active; }
	int  written() const { return m_written; }
	const QString& dir() const { return m_dir; }
	const QString& lastError() const { return m_error; }

private:
	bool m_active = false;
	bool m_isVideo = false;
	QString m_dir;			// detection_results 的完整路径
	QString m_imagePath;	// 图片任务的完整输出路径
	QString m_error;
	int m_written = 0;
	cv::VideoWriter m_writer;
};
