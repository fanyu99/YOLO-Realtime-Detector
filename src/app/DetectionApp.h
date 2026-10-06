#pragma once
#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QQmlParserStatus>
#include <QtQmlIntegration/QtQmlIntegration>
#include <filesystem>
#include <memory>

#include "AppError.h"
#include "DetectionManager.h"
#include "DetectionResultSaver.h"		// resultsDirFor()：结果目录约定的唯一定义
#include "DetectionTypes.h"

class DetectionImageProvider;

// 应用层门面：把 QML 的交互翻译成对 DetectionManager 的调用，
// 并把作业进度 / 结果 / 错误 / 日志回传给 QML。
//
// 分层约定：App 只认 jobId 这个不透明句柄，不接触 DetectionTask / TaskShared。
// 检测怎么跑（线程、队列、取消）全在 Manager / Worker 那一层。
class DetectionApp : public QObject, public QQmlParserStatus
{
	Q_OBJECT
	QML_ELEMENT
	Q_INTERFACES(QQmlParserStatus)
	Q_PROPERTY(QUrl filePath READ filePath WRITE setFilePath NOTIFY filePathChanged)			// 当前选择的文件(图片/视频)
	Q_PROPERTY(QUrl modelPath READ modelPath WRITE setModelPath NOTIFY modelPathChanged)		// 当前模型路径
	Q_PROPERTY(DetectorConfig config READ config NOTIFY configChanged)							// 当前配置(供侧边栏展示)
	Q_PROPERTY(QStringList classNames READ classNames NOTIFY configChanged)						// 类别名列表
	Q_PROPERTY(float progress READ progress NOTIFY progressChanged)								// 当前作业进度 0..1
	Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)											// 是否有作业在跑
	Q_PROPERTY(bool progressIndeterminate READ progressIndeterminate NOTIFY progressChanged)	// 总量未知(摄像头)
	Q_PROPERTY(int frameRevision READ frameRevision NOTIFY frameRevisionChanged)				// 帧刷新计数
	Q_PROPERTY(bool saveResultsEnabled READ saveResultsEnabled NOTIFY saveOptionsChanged)	// 是否保存推理结果
	Q_PROPERTY(QUrl saveDir READ saveDir NOTIFY saveOptionsChanged)							// 结果保存根目录
	Q_PROPERTY(QString bannerText READ bannerText NOTIFY bannerChanged)						// 横幅文本(空则不显示)
	Q_PROPERTY(BannerLevel bannerLevel READ bannerLevel NOTIFY bannerChanged)				// 横幅级别(决定配色)
	Q_PROPERTY(int resultCount READ resultCount NOTIFY reviewChanged)						// 回看缓冲里的结果数
	Q_PROPERTY(int resultIndex READ resultIndex NOTIFY reviewChanged)						// 当前回看位置(-1=跟随实时)
	Q_PROPERTY(bool reviewing READ reviewing NOTIFY reviewChanged)							// 是否处于回看模式
	Q_PROPERTY(QString resultInfo READ resultInfo NOTIFY reviewChanged)						// 回看条目的说明文字

public:
	// 横幅级别：决定 QML 侧的配色。一条横幅槽位，避免多条堆叠。
	enum BannerLevel {
		BannerNone = 0,
		BannerWarning = 1,		// 非致命提示，例如"输入尺寸已按模型自动对齐"
		BannerSuccess = 2,		// 成功提示，例如"结果已保存到 ..."
	};
	Q_ENUM(BannerLevel)

	explicit DetectionApp(QObject* parent = nullptr);
	~DetectionApp() override;

	// QQmlParserStatus：构造函数里对象还没和 engine 关联，
	// 必须等 componentComplete() 才能拿到 engine 去注册 image provider
	void classBegin() override {}
	void componentComplete() override;

	// Getter
	QUrl modelPath() const;
	QUrl filePath() const;
	DetectorConfig config() const;
	QStringList classNames() const;
	float progress() const { return m_progress; }
	bool busy() const { return m_busy; }
	bool progressIndeterminate() const { return m_progressIndeterminate; }
	int frameRevision() const { return m_frameRevision; }
	bool saveResultsEnabled() const { return m_saveEnabled; }
	QUrl saveDir() const;
	QString bannerText() const { return m_bannerText; }
	BannerLevel bannerLevel() const { return m_bannerLevel; }
	int resultCount() const { return static_cast<int>(m_review.size()); }
	int resultIndex() const { return m_reviewIndex; }
	bool reviewing() const { return m_reviewIndex >= 0; }
	QString resultInfo() const;

	// 供 QML 调用
	Q_INVOKABLE void setModelPath(const QUrl& path);
	Q_INVOKABLE void setFilePath(const QUrl& path);
	Q_INVOKABLE void setConfig(const DetectorConfig& config);
	Q_INVOKABLE void setConfig(const QUrl& model_path, const QStringList& class_names, float conf_confidence, float iou_threshold, int num_threads, int device_id, int input_width, int input_height, bool use_cuda = true);
	Q_INVOKABLE void onDetectClicked();							// qml 检测按钮点击
	Q_INVOKABLE void onDetectCameraClicked(int camera_id);		// qml 摄像头检测按钮点击(带设备 id)
	Q_INVOKABLE quint64 submitBatch(const QList<QUrl>& urls);	// qml 批量检测：一个 Job 含多个 Task
	Q_INVOKABLE void cancelCurrentJob();						// qml 停止按钮
	Q_INVOKABLE void dismissBanner();							// qml 关闭横幅
	Q_INVOKABLE QString frameSource() const;					// "image://detection/latest?rev=N"
	Q_INVOKABLE void setSaveOptions(bool enabled, const QUrl& dir);	// 设置"保存推理结果"开关与目录
	// 结果回看：只推进 UI 侧的索引，**完全不碰 worker** —— 推理照常全速跑，
	// UI 看得慢只是缓冲里少留几帧，不会拖慢检测。
	Q_INVOKABLE void nextResult();
	Q_INVOKABLE void prevResult();
	Q_INVOKABLE void followLatest();							// 退出回看，跟随实时画面

signals:
	void modelPathChanged();
	void filePathChanged();
	void configChanged();
	void progressChanged();
	void busyChanged();
	void frameRevisionChanged();
	void bannerChanged();
	void reviewChanged();
	void saveOptionsChanged();
	void errorOccurred(const AppError& error);
	void imageDetectionFinished(const DetectionResult& result);							// 图片检测完成
	void videoDetectionFinished(const QVector<DetectionResult>& videoDetectionResult);	// 视频/摄像头检测完成
	void batchFinished(quint64 jobId, int totalTasks, int succeeded, int failed);		// 批量作业完成(带汇总)

private:
	void reportError(const AppError& error);								// 统一上报错误：发信号 + 写日志
	AppError mapDetectionErrortoAppError(const DetectorError& error) const;	// 检测器错误 -> 应用层错误
	QString configSummary() const;											// 生成配置摘要文本(用于日志)
	// 实际生效的保存目录：开关关掉时为空（worker 见到空目录就不保存）
	// 实际生效的结果目录：开关关掉时为空（worker 见到空目录就不保存）。
	// 子目录名在这里组合一次，约定只有 DetectionResultSaver::resultsDirFor() 一处定义。
	QString effectiveResultsDir() const {
		return m_saveEnabled ? DetectionResultSaver::resultsDirFor(m_saveDir) : QString();
	}
	void setBusy(bool busy);
	void setState(quint64 currentJobId,bool progressInterminate, bool busy ); // 设置App状态
	// Manager 的作业事件 -> App 的 QML 接口
	void onJobStarted(quint64 jobId);
	void onJobProgress(quint64 jobId, float progress);
	void onFrameReady(quint64 jobId, const DetectionResult& result, const QImage& frame);
	void onJobFinished(quint64 jobId, DetectionJobType type,
					   const QVector<DetectionResult>& results,
					   int totalTasks, int failedTasks);
	void onJobFailed(quint64 jobId, const DetectorError& error);
	void onJobCancelled(quint64 jobId);
	void onJobSaved(quint64 jobId, const QString& dir, int fileCount);	// 结果已落盘 -> 成功横幅
	void onDetectorWarning(const DetectorWarning& warning);		// 检测器警告 -> 横幅
	void showBanner(const QString& text, BannerLevel level);	// 统一的横幅出口
	void onConfigAdjusted(const DetectorConfig& config);		// 检测器对齐了配置尺寸 -> 同步本地副本

	std::unique_ptr<DetectionManager> m_manager;
	DetectionImageProvider* m_frameProvider = nullptr;		// 帧提供器,所有权归 QML engine

	// 结果回看条目。帧本身存在 DetectionImageProvider 的有界缓冲里（按 seq 索引），
	// 这里只留元信息 —— 避免同一份像素在两个地方各存一份。
	struct ReviewItem {
		quint64 seq = 0;
		QString name;
		int objectCount = 0;
		long long durationMs = 0;
	};
	void resetReview();										// 新作业/换文件时清空回看缓冲
	std::filesystem::path m_filePath;						// 文件路径(图片/视频)
	DetectorConfig m_config;								// 检测器配置
	quint64 m_currentJobId = 0;								// 当前作业(0 表示空闲)
	// 注意：这里刻意不保存"当前作业是什么种类"。种类随 jobFinished 一起回来，
	// 所以不存在需要跨事件同步维护的字段（早期版本用 JobType+IsBatch 两个字段，
	// 实测有 12 个维护点且漏清过一处）。
	float m_progress = 0.0f;
	bool m_busy = false;
	bool m_progressIndeterminate = false;
	int m_frameRevision = 0;
	bool m_saveEnabled = false;								// 是否保存推理结果(由 QML 设置)
	QString m_saveDir;										// 保存根目录(实际写到其下 detection_results/)
	QString m_bannerText;									// 最近一条横幅文本(空表示无)
	BannerLevel m_bannerLevel = BannerNone;					// 横幅级别
	QVector<ReviewItem> m_review;							// 回看缓冲的元信息(有界)
	int m_reviewIndex = -1;									// -1 = 跟随实时画面
	QElapsedTimer m_frameThrottle;							// 限制 revision 的通知频率
};
