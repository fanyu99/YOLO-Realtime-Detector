#pragma once
#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QtQmlIntegration/QtQmlIntegration>
#include <QQmlEngine>
#include <QJSEngine>
#include <filesystem>
#include <memory>
#include "AppError.h"
#include "DetectionManager.h"
#include "DetectionResultSaver.h"		
#include "DetectionTypes.h"
#include "FrameMail.h"
#include "DetectionOverlayItem.h"
#include "DisplayPolicy.h"
#include "ClassPalette.h"

class DetectionImageProvider;
class PerformanceApp;

// 对接QML和下层的Manager
// 把作业进度 / 结果 / 错误 / 日志回传给 QML。
class DetectionApp : public QObject
{
	Q_OBJECT
	QML_ELEMENT
	QML_SINGLETON
	Q_PROPERTY(QUrl filePath READ filePath WRITE setFilePath NOTIFY filePathChanged)			// 当前选择的文件(图片/视频)
	Q_PROPERTY(QUrl modelPath READ modelPath WRITE setModelPath NOTIFY modelPathChanged)		// 当前模型路径
	Q_PROPERTY(DetectorConfig config READ config NOTIFY configChanged)							// 当前配置(供侧边栏展示)
	Q_PROPERTY(QStringList classNames READ classNames NOTIFY configChanged)						// 类别名列表
	Q_PROPERTY(float progress READ progress NOTIFY progressChanged)								// 当前作业进度 0..1
	Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)											// 是否有作业在跑
	Q_PROPERTY(bool progressIndeterminate READ progressIndeterminate NOTIFY progressChanged)	// 总量未知(摄像头)
	Q_PROPERTY(int frameRevision READ frameRevision NOTIFY frameRevisionChanged)				// 帧刷新计数
	//当前的帧源URL
	Q_PROPERTY(QString frameSource READ frameSource NOTIFY frameRevisionChanged)
	Q_PROPERTY(bool saveResultsEnabled READ saveResultsEnabled NOTIFY saveOptionsChanged)	// 是否保存推理结果
	Q_PROPERTY(QUrl saveDir READ saveDir NOTIFY saveOptionsChanged)							// 结果保存根目录
	Q_PROPERTY(QString bannerText READ bannerText NOTIFY bannerChanged)						// 横幅文本(空则不显示)
	Q_PROPERTY(BannerLevel bannerLevel READ bannerLevel NOTIFY bannerChanged)				// 横幅级别(决定配色)
	Q_PROPERTY(int resultCount READ resultCount NOTIFY reviewChanged)						// 回看缓冲里的结果数
	Q_PROPERTY(int resultIndex READ resultIndex NOTIFY reviewChanged)						// 当前回看位置(-1=跟随实时)
	Q_PROPERTY(bool reviewing READ reviewing NOTIFY reviewChanged)							// 是否处于回看模式
	Q_PROPERTY(QString resultInfo READ resultInfo NOTIFY reviewChanged)						// 回看条目的说明文字
	Q_PROPERTY(QString preferredViewMode READ preferredViewMode NOTIFY preferredViewModeChanged)	// 预览模式(用户模式)
	Q_PROPERTY(QString activeViewMode READ activeViewMode NOTIFY activeViewModeChanged)			// 实际生效的预览模式
	Q_PROPERTY(QList<ClassColorItem> clsClrInfo READ clsClrInfo NOTIFY clsClrInfoChanged)

public:
	using  AppPtr = std::unique_ptr<DetectionApp>;
	// 获取单例，注意，先调用initialize()初始化配置后才能调用instance()
	static DetectionApp* initialize ( const DetectorConfig& config, const DisplayPolicy& displayPolicy, PerformanceApp* perfApp);	// 初始化配置
	static DetectionApp* instance ();	// 未初始化返回nullptr
	static DetectionApp* create ( QQmlEngine* qmlEngine, QJSEngine* jsEngine );
	// 横幅级别（横幅颜色）
	enum BannerLevel {
		BannerNone = 0,
		BannerWarning = 1,		// 警告
		BannerSuccess = 2,		// 成功提示
	};
	Q_ENUM(BannerLevel)

	~DetectionApp() override;

	// Getter
	QList<ClassColorItem>clsClrInfo ()const;
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
	// 预览模式(用户模式)：偏好，持久化到 app.json 的 display.view_mode
	QString preferredViewMode() const { return ViewMode2String(m_displayPolicy.preferredMode()); }
	// 实际生效的预览模式：由 App 在链路重建后写回，未写回时跟回用户模式
	QString activeViewMode() const { return ViewMode2String(m_displayPolicy.activeMode()); }

	// 供 QML 调用
	Q_INVOKABLE void setFlowMode( const QString& flow_mode );	// 设置 CPU/GPU 模式
	Q_INVOKABLE void setViewMode( const QString& viewMode );	// 设置预览模式(离线/实时)
	Q_INVOKABLE QString effectiveFlowMode()const; // 获取当前实际生效的模式
	Q_INVOKABLE void setModelPath(const QUrl& path);
	Q_INVOKABLE void setFilePath(const QUrl& path);
	Q_INVOKABLE void setConfig(const DetectorConfig& config);
	Q_INVOKABLE void setConfig(const QUrl& model_path, const QStringList& class_names, float conf_confidence, float iou_threshold, int num_threads, int device_id, int input_width, int input_height,const QString& flow_mode);

	Q_INVOKABLE void onDetectClicked();							// qml 检测按钮点击
	Q_INVOKABLE void runSampleDetection();						// 一键示例检测
	Q_INVOKABLE void onDetectHikCameraClicked(const QString& serial, const QString& triggerMode);	// qml 海康相机检测(带序列号)
	Q_INVOKABLE void onDetectFfmpegDeviceClicked(const QString& deviceName, const QString& formatName = QString());	// qml Ffmpeg设备检测(带设备名)
	Q_INVOKABLE QVariantList hikDevices() const;				// 枚举在线海康相机(供选择对话框填列表)
	Q_INVOKABLE QVariantList ffmpegVideoDevices(const QString& formatName = QString()) const;	// 枚举 ffmpeg video 输入设备(供选择对话框填列表，阻塞操作)
	Q_INVOKABLE quint64 submitBatch(const QList<QUrl>& urls);	// qml 批量检测：一个 Job 含多个 Task
	Q_INVOKABLE void cancelCurrentJob();						// qml 停止按钮
	Q_INVOKABLE void dismissBanner();							// qml 关闭横幅
	QString frameSource() const;					// 当前应显示的帧源URL
	Q_INVOKABLE void setSaveOptions(bool enabled, const QUrl& dir);	// 设置"保存推理结果"开关与目录
	Q_INVOKABLE void nextResult();
	Q_INVOKABLE void prevResult();
	Q_INVOKABLE void followLatest();							// 退出回看，跟随实时画面

	// 叠加层的注册/反注册
	void installOverlay ( DetectionOverlayItem* overlay );
	void uninstallOverlay ( DetectionOverlayItem* overlay );
	// 借用性能监测器指针
	IPerformanceMonitor* monitor () const;

signals:
	void   clsClrInfoChanged ();
	void modelPathChanged();
	void filePathChanged();
	void configChanged();
	void progressChanged();
	void busyChanged();
	void frameRevisionChanged();
	void bannerChanged();
	void reviewChanged();
	void saveOptionsChanged();
	void preferredViewModeChanged();
	void activeViewModeChanged();
	void errorOccurred(const AppError& error);
	void imageDetectionFinished(const DetectionResult& result);							// 图片检测完成
	void videoDetectionFinished(const QVector<DetectionResult>& videoDetectionResult);	// 视频/摄像头检测完成
	void batchFinished(quint64 jobId, int totalTasks, int succeeded, int failed);		// 批量作业完成(带汇总)

private:
	void setClsClrInfo ( const QStringList& classNames ); // 重新设置类别名的时候重新设置
	void onFrameSnapshotPosted (); // 帧快照入邮箱,取走帧快照投递给Item
	void syncPerfMode (); // 同步性能监测器的 CPU/GPU 模式
	// 借用PerformanceApp并注入其监测器，需要在初始化配置前
	void setPerformanceApp ( PerformanceApp* perfApp );
	// 设置初始化配置
	void setInitialConfig ( const DetectorConfig& config );
	// 应用配置并写入配置文件中，persist判断是否需要写入
	void applyDetectorConfig ( const DetectorConfig& config, bool persist );
	// 初始化显示策略(不落盘)
	void setInitialDisplayPolicy ( const DisplayPolicy& policy );
	// 应用预览模式，persist判断是否需要写入配置文件
	void applyViewMode ( const ViewMode& mode, bool persist );

	// fallback自愈并发送信号
	void onFallbackOccurred( const DetectionError& error );	

	// 将字符串变为模式枚举
	std::optional<FlowMode> str2FlowMode( QStringView str ) const;

	void reportError(const AppError& error);								// 统一上报错误：发信号 + 写日志
	AppError mapDetectionErrortoAppError(const DetectionError& error) const;	// 检测器错误 -> 应用层错误
	// 由文件路径构造帧源描述，识别失败的设置为单图
	FrameSourceDescription makeFileDescription(QStringView path) const;
	// 获取示例资源根目录
	QString samplesRoot() const;
	// 触发模式字符串 -> 枚举（无效值回退 Off +记警告）
	CameraTriggerMode str2TriggerMode( QStringView str ) const;

	QString configSummary() const;											// 生成配置摘要文本(用于日志)
	// 实际生效的结果目录，关掉时为空
	QString effectiveResultsDir() const {
		return m_saveEnabled ? DetectionResultSaver::resultsDirFor(m_saveDir) : QString();
	}
	// 将当前的ViewMode、FlowMode、是否保存、Overlay是否可用映射为显示/保存需求
	DisplaySaveStatus makeDisplaySaveStatus();
	void setBusy(bool busy);
	void setState(quint64 currentJobId,bool progressInterminate, bool busy ); // 设置App状态
	// Manager 的作业事件 -> App 的 QML 接口
	void onJobStarted(quint64 jobId);
	void onJobProgress(quint64 jobId, float progress);
	void onFrameReady(quint64 jobId, const DetectionResult& result, const QImage& frame);
	void onJobFinished(quint64 jobId, DetectionJobType type,
					   const QVector<DetectionResult>& results,
					   int totalTasks, int failedTasks);
	void onJobFailed(quint64 jobId, const DetectionError& error);
	void onJobCancelled(quint64 jobId);
	void onJobSaved(quint64 jobId, QStringView dir, int fileCount);	// 结果已落盘 -> 成功横幅
	void onDetectorWarning(const DetectorWarning& warning);		// 检测器警告 -> 横幅
	void showBanner(QStringView text, BannerLevel level);	// 统一的横幅出口
	void onConfigAdjusted(const DetectorConfig& config);		// 检测器对齐了配置尺寸 -> 同步本地副本


	explicit DetectionApp ( QObject* parent = nullptr );
	inline static  AppPtr m_instance;  // 单例智能指针
	DetectionOverlayItem* m_overlay{ nullptr };	// 检测框叠加器，借用指针
	FrameMailPtr m_frameMail{nullptr}; // 帧快照邮箱，Worker写入快照
	quint64 m_lastReportedDropped = 0;	// 已上报过的邮箱丢弃计数（读增量用），仅 GUI 线程读写
	std::unique_ptr<DetectionManager> m_manager{nullptr};
	PerformanceApp* m_performanceApp = nullptr;			 //绑定的性能App，借用指针
	DetectionImageProvider* m_frameProvider = nullptr;		// 帧提供器,所有权归 QML engine
	DisplayPolicy m_displayPolicy;	// 显示策略(模式+后端)

	// 类别-颜色图例
	QList<ClassColorItem>m_classColorItems;

	// 结果回看条目
	// 回看只存序列号、目标数、耗时等元信息
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
	QElapsedTimer m_signalLimitTimer;							// 限制 revision 的通知频率
};
