#include <QtTest/QTest>
#include <QObject>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QQuickItem>
#include <QSGNode>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <QTextStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QDebug>
#include <QVector>
#include <QPair>
#include <QSize>
#include <QStringList>
#include <cstdio>
#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>
#include <string>

#include <opencv2/opencv.hpp>

#include "PerformanceMonitor.h"
#include "PerformanceTypes.h"
#include "DisplayPolicy.h"
#include "DisplaySaveStatus.h"
#include "DetectionTypes.h"
#include "DetectionWorker.h"
#include "FrameMail.h"
#include "FrameSnapshot.h"
#include "OverlayTextureSource.h"
#include "DetectionResultSaver.h"
#include "AppConfigLoader.h"
#include "Logger.h"

/*
| 用户选的 ViewMode | 生效 FlowMode | 保存    | 实际后端          | 是否下载                |
| ------------- | ----------- | ----- | ------------- | ------------------- |
| Realtime      | GPU，互操作可用   | 关     | Overlay（零拷贝）  | 否，直接使用 GPU 像素       |
| Realtime      | GPU，互操作可用   | 开     | Overlay（零拷贝）  | 是，保存需要渲染            |
| Realtime      | GPU，互操作失败   | 任意    | Overlay（上传）   | 是，需要下载到 CPU 使用      |
| Realtime      | CPU         | 任意    | Overlay（上传）   | 否，直接使用 CPU 数据进行     |
| Offline       | 任意          | 任意    | ImageProvider | 是，需要渲染检测框和转为 QImage |
 */

namespace {
	constexpr int kVideoFrames = 90;			// 每个分辨率视频的帧数
	constexpr double kVideoFps = 30.0;			// 视频帧率
	constexpr int kWindowWidth = 1280;			// Overlay 测试窗口尺寸
	constexpr int kWindowHeight = 720;
	constexpr int kStartTimeoutMs = 120000;		// 模型加载 + 预热超时
	constexpr int kJobTimeoutMs = 15 * 60 * 1000;	// 单个视频任务超时
	constexpr int kReportIntervalMs = 100;		// 性能快照上报间隔

	QString testSourceDir() {
#ifdef QT_TESTCASE_SOURCEDIR
		return QStringLiteral( QT_TESTCASE_SOURCEDIR );
#else
		return QString();
#endif
	}
}

// 测试用的最小 Overlay：只负责驱动真实的 OverlayTextureSource，
// 不画检测框（框绘制没有采集点，不影响被测阶段）。
class TestOverlayItem final : public QQuickItem {
	Q_OBJECT
public:
	explicit TestOverlayItem( IPerformanceMonitor* monitor, QQuickItem* parent = nullptr )
		: QQuickItem( parent )
		, m_monitor( monitor )
		, m_source( std::make_unique<OverlayTextureSource>( monitor ) ) {
		setFlag( QQuickItem::ItemHasContents, true );
		setAcceptedMouseButtons( Qt::NoButton );
	}

	void setSnapshot( FrameSnapshotPtr snapshot ) {
		if (!snapshot || !snapshot->frame)
			return;
		{
			std::lock_guard<std::mutex> lock( m_mutex );
			m_pending = std::move( snapshot );
		}
		m_snapshotCount.fetch_add( 1, std::memory_order_relaxed );
		update();
	}

	void invalidateSource() { m_source->invalidate(); }

	int renderCount() const { return m_renderCount.load( std::memory_order_relaxed ); }
	int snapshotCount() const { return m_snapshotCount.load( std::memory_order_relaxed ); }

signals:
	void nativeChanged( bool ok );

protected:
	QSGNode* updatePaintNode( QSGNode* oldNode, UpdatePaintNodeData* ) override {
		FrameSnapshotPtr temp;
		{
			std::lock_guard<std::mutex> lock( m_mutex );
			if (m_pending) {
				m_last = std::move( m_pending );
				temp = m_last;
			}
		}
		if (!temp)
			return oldNode;

		m_renderCount.fetch_add( 1, std::memory_order_relaxed );
		if (m_monitor)
			m_monitor->recordStage( PerformanceStage::DisplayWait, frameClockNs() - temp->postTime );

		QSGNode* root = oldNode;
		QSGSimpleTextureNode* node = nullptr;
		if (!root) {
			root = new QSGNode;
			node = new QSGSimpleTextureNode;
			node->setFlag( QSGNode::OwnedByParent, true );
			node->setOwnsTexture( false );
			node->setFiltering( QSGTexture::Linear );
			root->appendChildNode( node );
		}
		else {
			node = static_cast<QSGSimpleTextureNode*>( root->firstChild() );
		}

		bool textureReady = false;
		if (QSGTexture* texture = m_source->textureFor( temp->frame, window() )) {
			node->setTexture( texture );
			node->setRect( QRectF( 0, 0, width(), height() ) );
			textureReady = true;
		}
		else {
			emit nativeChanged( false );
		}

		if (textureReady && m_monitor)
			m_monitor->recordStage( PerformanceStage::PresentLatency, frameClockNs() - temp->frameStartNs );

		return root;
	}

private:
	IPerformanceMonitor* m_monitor = nullptr;
	std::unique_ptr<OverlayTextureSource> m_source;
	mutable std::mutex m_mutex;
	FrameSnapshotPtr m_pending;
	FrameSnapshotPtr m_last;
	std::atomic<int> m_renderCount{ 0 };
	std::atomic<int> m_snapshotCount{ 0 };
};

// 测试预览模式矩阵各个阶段计时显示
class test_display_matrix : public QObject {
	Q_OBJECT
private slots:
	void initTestCase();
	void cleanupTestCase();

	// 输出打印预览模式矩阵（见上方）
	void printMatrixInfo();

	void test_realtime_gpuOk_noSave();
	void test_realtime_gpuOk_Save();

	void test_realtime_gpuFail_noSave();
	void test_realtime_gpuFail_Save();

	void test_offline_gpu_noSave();
	void test_offline_gpu_Save();
	void test_offline_cpu_noSave();
	void test_offline_cpu_Save();

	// CPU 流程较慢，放在最后
	void test_realtime_cpu_noSave();
	void test_realtime_cpu_Save();

private:
	struct VideoSpec {
		QString label;
		QSize size;
		QString path;
	};

	struct RunEnv {
		QString name;
		ViewMode viewMode = ViewMode::Offline;
		FlowMode flowMode = FlowMode::CPU;
		bool nativeInterop = false;
		bool save = false;
		bool needsOverlay = false;
	};

	struct StageAgg {
		quint64 count = 0;
		quint64 totalNs = 0;
		quint64 maxNs = 0;
	};

	struct RunResult {
		QString resolution;
		QString setupError;
		QString saveDir;
		quint64 recordedFrames = 0;
		quint64 successFrames = 0;
		quint64 failedFrames = 0;
		quint64 droppedFrames = 0;
		quint64 savedFrames = 0;
		int savedFileCount = 0;
		quint64 savedDiskBytes = 0;
		quint64 offlineImages = 0;
		bool offlineImageValid = false;
		quint64 objects = 0;
		quint64 relayPosts = 0;
		int overlaySnapshots = 0;
		int overlayRenders = 0;
		double windowMs = 0.0;
		double wallMs = 0.0;
		quint64 narrowTotalNs = 0;
		quint64 narrowSamples = 0;
		quint64 broadTotalNs = 0;
		quint64 broadSamples = 0;
		quint64 warmupNs = 0;
		int warmupIterations = 0;
		std::array<StageAgg, PerformanceStageCount> stages{};

		bool hasStage( PerformanceStage stage ) const {
			return stages[ static_cast<size_t>(stage) ].count > 0;
		}
		quint64 stageCount( PerformanceStage stage ) const {
			return stages[ static_cast<size_t>(stage) ].count;
		}
	};

	RunResult runOnce( const RunEnv& env, const VideoSpec& video );
	void runScenario( const RunEnv& env );
	void printResult( const QString& scenario, const QVector<RunResult>& runs );
	void printStats( const QString& title, const RunResult& result );
	static RunResult mergeRuns( const QVector<RunResult>& runs );
	static void accumulate( RunResult& result, const PerformanceSnapshot& snapshot );

	bool makeVideo( const QSize& size, const QString& path, QString& error );
	static double stageAvgMs( const RunResult& result, PerformanceStage stage );
	static double avgNsMs( quint64 totalNs, quint64 samples );

	std::unique_ptr<AppConfig> m_appConfig;
	QString m_workDir;	// 测试输出根目录：<exe目录>/test_display_matrix_output
	QVector<VideoSpec> m_videos;
	int m_saveSeq = 0;
};

void test_display_matrix::initTestCase()
{
	Logger::instance()->setLevel( Logger::Level::Warning );
	Logger::instance()->setConsoleEnabled( true );

	qRegisterMetaType<DetectionResult>();
	qRegisterMetaType<DetectionError>();
	qRegisterMetaType<DetectorWarning>();
	qRegisterMetaType<QVector<DetectionResult>>();

	// 测试输出放在测试程序所在目录，便于定位保存产物；测试结束时统一删除
	m_workDir = QDir( QCoreApplication::applicationDirPath() )
		.filePath( QStringLiteral( "test_display_matrix_output" ) );
	QVERIFY2( !m_workDir.isEmpty(), "测试输出目录为空" );
	QDir( m_workDir ).removeRecursively();
	QVERIFY2( QDir().mkpath( m_workDir ),
		qPrintable( QStringLiteral( "无法创建测试目录: " ) + m_workDir ) );

	// 与 main.cpp 一致：优先读 exe 同级的 settings/app.json
	const QString exeConfig = QCoreApplication::applicationDirPath() + QStringLiteral( "/settings/app.json" );
	const QString configPath = QFileInfo::exists( exeConfig ) ? exeConfig : QStringLiteral( "settings/app.json" );
	QVERIFY2( QFileInfo::exists( configPath ),
		qPrintable( QStringLiteral( "找不到配置文件: " ) + configPath ) );

	m_appConfig.reset( AppConfigLoader::loadAppConfig( configPath ) );
	QVERIFY2( m_appConfig, "AppConfigLoader 返回空配置" );
	for (const QString& warning : m_appConfig->warnings)
		qWarning().noquote() << "[AppConfig]" << warning;

	const DetectorConfig& cfg = m_appConfig->detectorConfig;
	QVERIFY2( !cfg.model_path.empty(), "app.json 未配置模型路径" );
	QVERIFY2( cfg.model_exists(), qPrintable( QStringLiteral( "模型不存在: " ) + cfg.modelPathText() ) );
	QVERIFY2( !cfg.class_names.isEmpty(), "app.json 未配置类别名" );

	QTextStream out( stdout );
	out << "[配置] " << configPath << "\n";
	out << "[模型] " << cfg.modelPathText() << "\n";
	out << "[类别] " << cfg.class_names.size() << " 个\n";
	out << "[输入] " << cfg.input_width << "x" << cfg.input_height
		<< "，偏好模式: " << cfg.preferredModeText() << "\n";

	const QVector<QPair<QString, QSize>> specs = {
		{ QStringLiteral( "1080p" ), QSize( 1920, 1080 ) },
		{ QStringLiteral( "2K" ), QSize( 2560, 1440 ) },
		{ QStringLiteral( "4K" ), QSize( 3840, 2160 ) },
	};
	for (const auto& spec : specs) {
		VideoSpec video;
		video.label = spec.first;
		video.size = spec.second;
		video.path = QDir( m_workDir ).filePath( QStringLiteral( "video_%1.mp4" ).arg( video.label ) );
		QString error;
		QVERIFY2( makeVideo( video.size, video.path, error ), qPrintable( error ) );
		m_videos.append( video );
		out << "[视频] " << video.label << " " << video.size.width() << "x" << video.size.height()
			<< " -> " << video.path << " (" << QFileInfo( video.path ).size() / 1024 << " KB)\n";
	}

	printMatrixInfo();
}

void test_display_matrix::cleanupTestCase()
{
	QTextStream( stdout ) << "[清理] 测试目录: " << m_workDir << "\n";
	m_videos.clear();
	if (!m_workDir.isEmpty())
		QDir( m_workDir ).removeRecursively();
}

// 输出打印预览模式矩阵
void test_display_matrix::printMatrixInfo()
{
	QTextStream out( stdout );
	out << "\n预览模式测试矩阵:\n";
	out << "函数\tViewMode\tFlowMode\t互操作\t保存\tOverlay\t预期关键阶段\n";
	out << "test_realtime_gpuOk_noSave\tRealtime\tGPU\t可用\t否\t有\tConvertKernel>0, Frame2Cpu=0, Save=0\n";
	out << "test_realtime_gpuOk_Save\tRealtime\tGPU\t可用\t是\t有\tConvertKernel>0, Frame2Cpu/RenderImage/Save>0\n";
	out << "test_realtime_gpuFail_noSave\tRealtime\tGPU\t不可用\t否\t有\tConvertKernel=0, Frame2Cpu>0, RenderImage=0\n";
	out << "test_realtime_gpuFail_Save\tRealtime\tGPU\t不可用\t是\t有\tConvertKernel=0, Frame2Cpu/RenderImage/Save>0\n";
	out << "test_offline_gpu_noSave\tOffline\tGPU\t-\t否\t无\tToQImage>0, DisplayWait=0\n";
	out << "test_offline_gpu_Save\tOffline\tGPU\t-\t是\t无\tToQImage/RenderImage/Save>0, DisplayWait=0\n";
	out << "test_offline_cpu_noSave\tOffline\tCPU\t-\t否\t无\tToQImage>0, DisplayWait=0\n";
	out << "test_offline_cpu_Save\tOffline\tCPU\t-\t是\t无\tToQImage/RenderImage/Save>0, DisplayWait=0\n";
	out << "test_realtime_cpu_noSave\tRealtime\tCPU\t-\t否\t有\tConvertKernel=0, Frame2Cpu≈0, DisplayWait>0\n";
	out << "test_realtime_cpu_Save\tRealtime\tCPU\t-\t是\t有\tConvertKernel=0, Frame2Cpu/RenderImage/Save>0\n\n";
}

bool test_display_matrix::makeVideo( const QSize& size, const QString& path, QString& error )
{
	const int w = size.width();
	const int h = size.height();
	if (w <= 0 || h <= 0) {
		error = QStringLiteral( "视频尺寸非法" );
		return false;
	}

	// 优先用仓库里的示例图做背景；找不到就用合成的移动色块
	cv::Mat base;
	QStringList candidates;
	candidates << QCoreApplication::applicationDirPath() + QStringLiteral( "/examples/images/1 (369).jpg" );
	if (!testSourceDir().isEmpty())
		candidates << testSourceDir() + QStringLiteral( "/../../examples/images/1 (369).jpg" );
	for (const QString& candidate : candidates) {
		if (candidate.isEmpty() || !QFileInfo::exists( candidate ))
			continue;
		base = cv::imread( candidate.toStdString() );
		if (!base.empty())
			break;
	}
	if (base.empty())
		base = cv::Mat( h, w, CV_8UC3, cv::Scalar( 30, 30, 30 ) );
	else
		cv::resize( base, base, cv::Size( w, h ) );

	// 放大 1.2 倍，逐帧裁切形成平移动画，避免画面完全静止
	cv::Mat panned;
	cv::resize( base, panned, cv::Size( static_cast<int>( w * 1.2 ), static_cast<int>( h * 1.2 ) ) );

	cv::VideoWriter writer;
	const int codecs[] = {
		cv::VideoWriter::fourcc( 'a', 'v', 'c', '1' ),
		cv::VideoWriter::fourcc( 'H', '2', '6', '4' ),
		cv::VideoWriter::fourcc( 'm', 'p', '4', 'v' ),
	};
	for (int fourcc : codecs) {
		QFile::remove( path );
		writer.open( path.toStdString(), fourcc, kVideoFps, cv::Size( w, h ) );
		if (writer.isOpened())
			break;
	}
	if (!writer.isOpened()) {
		error = QStringLiteral( "无法创建测试视频: " ) + path;
		return false;
	}

	const int maxX = panned.cols - w;
	const int maxY = panned.rows - h;
	const double twoPi = 6.283185307179586;
	cv::Mat canvas( h, w, CV_8UC3 );
	for (int i = 0; i < kVideoFrames; ++i) {
		const double t = static_cast<double>( i ) / static_cast<double>( std::max( 1, kVideoFrames - 1 ) );
		const int x = static_cast<int>( maxX * t );
		const int y = maxY / 2;
		panned( cv::Rect( x, y, w, h ) ).copyTo( canvas );

		const int boxW = std::max( 32, w / 8 );
		const int boxH = std::max( 32, h / 8 );
		const int cx = static_cast<int>( ( w - boxW ) * ( 0.5 + 0.5 * std::sin( t * twoPi ) ) );
		cv::rectangle( canvas, cv::Rect( cx, h / 2 - boxH / 2, boxW, boxH ),
			cv::Scalar( 0, 200, 255 ), -1 );
		cv::putText( canvas, "frame " + std::to_string( i ),
			cv::Point( std::max( 10, w / 64 ), std::max( 30, h / 32 ) ),
			cv::FONT_HERSHEY_SIMPLEX, std::max( 0.8, h / 1080.0 ),
			cv::Scalar( 255, 255, 255 ), 2 );
		writer.write( canvas );
	}
	writer.release();

	if (QFileInfo( path ).size() <= 0) {
		error = QStringLiteral( "测试视频文件为空: " ) + path;
		return false;
	}
	return true;
}

void test_display_matrix::accumulate( RunResult& result, const PerformanceSnapshot& snapshot )
{
	result.recordedFrames += static_cast<quint64>( snapshot.m_overall.recordedFrames );
	result.successFrames += static_cast<quint64>( snapshot.m_overall.successfulFrames );
	result.failedFrames += static_cast<quint64>( snapshot.m_overall.failedFrames );

	// 只看有帧的窗口，避免任务结束后等待快照的空白窗口拉低帧率
	if (snapshot.m_overall.recordedFrames > 0)
		result.windowMs += snapshot.m_context.windowDurationMs;

	if (snapshot.m_overall.warmupNs > 0)
		result.warmupNs = snapshot.m_overall.warmupNs;
	if (snapshot.m_overall.warmupIterations > 0)
		result.warmupIterations = snapshot.m_overall.warmupIterations;

	const quint64 broadSamples = static_cast<quint64>( snapshot.m_overall.recordedFrames );
	result.broadTotalNs += snapshot.m_overall.broadEndToEndNs * broadSamples;
	result.broadSamples += broadSamples;

	for (const StageStatistics& stat : snapshot.m_stages) {
		StageAgg& agg = result.stages[ static_cast<size_t>(stat.stage) ];
		agg.count += stat.sampleCount;
		agg.totalNs += stat.averageNs * stat.sampleCount;
		agg.maxNs = std::max( agg.maxNs, stat.maximumNs );
		if (stat.stage == PerformanceStage::end2End) {
			result.narrowTotalNs += stat.averageNs * stat.sampleCount;
			result.narrowSamples += stat.sampleCount;
		}
	}
}

test_display_matrix::RunResult test_display_matrix::runOnce( const RunEnv& env, const VideoSpec& video )
{
	RunResult result;
	result.resolution = video.label;

	PerformanceConfig perfConfig;
	perfConfig.m_level = PerformanceLevel::Full;
	perfConfig.m_reportInterval = kReportIntervalMs;

	PerformanceMonitor monitor;
	monitor.setConfig( perfConfig );
	QObject::connect( &monitor, &PerformanceMonitor::snapshotReady, &monitor,
		[&result]( const PerformanceSnapshot& snapshot ) { accumulate( result, snapshot ); } );

	FrameMail mail;
	mail.setNativeOk( env.nativeInterop );

	QObject relayGuard;		// 队列连接的接收上下文，析构时清掉未处理事件
	QTimer renderTimer;		// 固定 60fps 离屏渲染，驱动真实 Overlay 路径
	std::unique_ptr<QQuickWindow> window;
	TestOverlayItem* overlay = nullptr;
	QString saveRoot;
	QString resultsDir;
	quint64 lastDropped = 0;
	quint64 droppedTotal = 0;
	quint64 relayPosts = 0;
	QImage lastImage;
	std::atomic<int> imageCount{ 0 };

	DetectorConfig cfg = m_appConfig->detectorConfig;
	cfg.preferred_mode = env.flowMode;
	cfg.active_mode = env.flowMode;

	DetectionWorker worker;
	worker.setConfig( cfg );
	worker.setMonitor( &monitor );
	worker.setFrameMail( &mail );

	bool cleaned = false;
	auto cleanup = [&] {
		if (cleaned)
			return;
		cleaned = true;
		renderTimer.stop();
		QObject::disconnect( &worker, &DetectionWorker::frameSnapshotPosted, &relayGuard, nullptr );
		if (overlay)
			QObject::disconnect( overlay, &TestOverlayItem::nativeChanged, &relayGuard, nullptr );
		QCoreApplication::removePostedEvents( &relayGuard );
		monitor.setIsRunning( false );
		if (worker.isRunning())
			worker.stop();
		if (window) {
			window->hide();
			window->close();
			QCoreApplication::processEvents();
			window.reset();
		}
	};

	// Overlay 消费者（仅实时模式）
	if (env.needsOverlay) {
		window = std::make_unique<QQuickWindow>();
		window->setColor( Qt::black );
		window->resize( kWindowWidth, kWindowHeight );
		overlay = new TestOverlayItem( &monitor, window->contentItem() );
		overlay->setSize( QSizeF( kWindowWidth, kWindowHeight ) );

		QObject::connect( window.get(), &QQuickWindow::sceneGraphError, &relayGuard,
			[]( QQuickWindow::SceneGraphError error, const QString& message ) {
				qWarning().noquote() << "[Overlay] sceneGraphError" << static_cast<int>( error ) << message;
			} );

		QObject::connect( &worker, &DetectionWorker::frameSnapshotPosted, &relayGuard,
			[&mail, overlay, &monitor, &lastDropped, &droppedTotal, &relayPosts]() {
				++relayPosts;
				const quint64 dropped = mail.dropped();
				if (dropped > lastDropped) {
					const quint64 delta = dropped - lastDropped;
					for (quint64 i = 0; i < delta; ++i)
						monitor.recordStage( PerformanceStage::DisplayDropped, 0 );
					droppedTotal += delta;
					lastDropped = dropped;
				}
				if (auto snapshot = mail.take())
					overlay->setSnapshot( std::move( snapshot ) );
			}, Qt::QueuedConnection );
		QObject::connect( overlay, &TestOverlayItem::nativeChanged, &relayGuard,
			[&mail]( bool ok ) { mail.setNativeOk( ok ); }, Qt::QueuedConnection );
		QObject::connect( window.get(), &QQuickWindow::sceneGraphInvalidated, overlay,
			[overlay]() { overlay->invalidateSource(); }, Qt::DirectConnection );

		window->show();
		if (!QTest::qWaitForWindowExposed( window.get(), 10000 )) {
			result.setupError = QStringLiteral( "QQuickWindow 未能显示，无法测试 Overlay 路径" );
			cleanup();
			return result;
		}
		// 当前测试会话里窗口不会持续合成，用 grabWindow 按固定帧率驱动离屏渲染。
		window->hide();
		renderTimer.setInterval( 16 );
		QObject::connect( &renderTimer, &QTimer::timeout, &relayGuard, [&window]() {
			if (window)
				window->grabWindow();
		} );
		renderTimer.start();
	}

	// 离线 QImage 消费（仅离线模式），DirectConnection 在 worker 线程里保留最新一张
	if (env.viewMode == ViewMode::Offline) {
		QObject::connect( &worker, &DetectionWorker::taskResult, &worker,
			[&lastImage, &imageCount]( quint64, const DetectionResult&, const QImage& frame ) {
				if (frame.isNull())
					return;
				lastImage = frame;
				imageCount.fetch_add( 1, std::memory_order_relaxed );
			}, Qt::DirectConnection );
	}

	// 等待 Session 构造 + 预热完成
	bool started = false;
	bool startFailed = false;
	QString startError;
	{
		QEventLoop loop;
		QObject::connect( &worker, &DetectionWorker::started, &loop,
			[&] { started = true; loop.quit(); } );
		QObject::connect( &worker, &DetectionWorker::detectorFailed, &loop,
			[&]( const DetectionError& error ) {
				startFailed = true;
				startError = error.errorMessage;
				loop.quit();
			} );
		QTimer::singleShot( kStartTimeoutMs, &loop, [&] { loop.quit(); } );

		if (!worker.start()) {
			result.setupError = QStringLiteral( "DetectionWorker::start() 返回失败" );
			cleanup();
			return result;
		}
		loop.exec();
	}
	if (!started) {
		result.setupError = startFailed
			? QStringLiteral( "检测器启动失败: " ) + startError
			: QStringLiteral( "等待检测器启动超时" );
		cleanup();
		return result;
	}

	// 预热阶段已由 Session 记录；不能再 setConfig 重置窗口，否则 Warmup 阶段样本会丢
	monitor.setIsRunning( true );

	if (env.save) {
		saveRoot = QDir( m_workDir ).filePath( QStringLiteral( "save_%1_%2" ).arg( ++m_saveSeq ).arg( video.label ) );
		QDir( saveRoot ).removeRecursively();
		if (!QDir().mkpath( saveRoot )) {
			result.setupError = QStringLiteral( "无法创建保存目录: " ) + saveRoot;
			cleanup();
			return result;
		}
		resultsDir = DetectionResultSaver::resultsDirFor( saveRoot );
		result.saveDir = resultsDir;
	}

	const DisplaySaveStatus status = DisplaySaveStatus::getDisplaySaveStatus(
		env.viewMode, env.flowMode == FlowMode::GPU, env.needsOverlay, resultsDir );

	auto shared = std::make_shared<TaskShared>( 1, DetectionJobType::SingleVideo, status );
	auto task = std::make_unique<DetectionTask>();
	task->taskId = 1;
	task->jobId = 1;
	task->sourceDescription.type = FrameSourceType::VideoFile;
	task->sourceDescription.filePath = video.path;
	task->shared = shared;
	task->accumulateResults = true;

	bool completed = false;
	bool jobFailed = false;
	bool jobCancelled = false;
	bool jobTimedOut = false;
	QString jobError;
	int jobFrameCount = 0;
	quint64 objectTotal = 0;
	bool submitted = false;
	QElapsedTimer wallClock;
	wallClock.start();
	{
		QEventLoop loop;
		QObject::connect( &worker, &DetectionWorker::taskCompleted, &loop,
			[&]( quint64, const QVector<DetectionResult>& results ) {
				completed = true;
				jobFrameCount = results.size();
				for (const DetectionResult& frame : results)
					objectTotal += static_cast<quint64>( frame.boxes.size() );
				loop.quit();
			} );
		QObject::connect( &worker, &DetectionWorker::taskFailed, &loop,
			[&]( quint64, const DetectionError& error ) {
				jobFailed = true;
				jobError = error.errorMessage;
				loop.quit();
			} );
		QObject::connect( &worker, &DetectionWorker::taskCancelled, &loop,
			[&]( quint64 ) {
				jobCancelled = true;
				loop.quit();
			} );
		QTimer::singleShot( kJobTimeoutMs, &loop, [&] {
			jobTimedOut = true;
			loop.quit();
		} );

		submitted = worker.submit( std::move( task ) );
		if (submitted)
			loop.exec();
	}
	result.wallMs = static_cast<double>( wallClock.elapsed() );

	if (!submitted) {
		result.setupError = QStringLiteral( "任务投递失败" );
		cleanup();
		return result;
	}
	if (jobTimedOut)
		shared->requestCancel();

	// 给 Overlay 渲染线程留出消费最后几帧的时间，再冲刷最后一个统计窗口
	if (env.needsOverlay)
		QTest::qWait( 300 );
	QTest::qWait( kReportIntervalMs + 50 );
	monitor.setIsRunning( false );
	if (worker.isRunning())
		worker.stop();

	result.droppedFrames = droppedTotal;
	result.savedFrames = shared->savedFiles.load( std::memory_order_relaxed );
	result.offlineImages = static_cast<quint64>( imageCount.load( std::memory_order_relaxed ) );
	result.offlineImageValid = !lastImage.isNull();
	result.objects = objectTotal;
	result.relayPosts = relayPosts;
	result.overlaySnapshots = overlay ? overlay->snapshotCount() : 0;
	result.overlayRenders = overlay ? overlay->renderCount() : 0;

	if (!resultsDir.isEmpty()) {
		const QFileInfoList files = QDir( resultsDir ).entryInfoList( QDir::Files );
		result.savedFileCount = files.size();
		for (const QFileInfo& file : files)
			result.savedDiskBytes += static_cast<quint64>( file.size() );
	}

	if (jobTimedOut)
		result.setupError = QStringLiteral( "任务执行超时" );
	else if (jobFailed)
		result.setupError = QStringLiteral( "任务失败: " ) + jobError;
	else if (jobCancelled)
		result.setupError = QStringLiteral( "任务被取消" );
	else if (!completed)
		result.setupError = QStringLiteral( "任务未完成" );

	// 帧数取任务结果（精确）。监控快照在窗口切换边界可能少记 1~2 个样本，
	// 阶段统计仍以快照为准，二者差异会在样本数上体现。
	if (completed && jobFrameCount > 0) {
		result.recordedFrames = static_cast<quint64>( jobFrameCount );
		result.successFrames = static_cast<quint64>( jobFrameCount );
	}
	else if (result.recordedFrames == 0 && jobFrameCount > 0) {
		result.recordedFrames = static_cast<quint64>( jobFrameCount );
		result.successFrames = static_cast<quint64>( jobFrameCount );
	}

	cleanup();
	return result;
}

void test_display_matrix::runScenario( const RunEnv& env )
{
	QVector<RunResult> results;
	QStringList errors;
	bool skipInterop = false;

	for (const VideoSpec& video : m_videos) {
		RunResult result = runOnce( env, video );
		results.append( result );
		if (!result.setupError.isEmpty()) {
			errors << QStringLiteral( "%1/%2: %3" ).arg( env.name, video.label, result.setupError );
			break;
		}

		if (result.recordedFrames == 0)
			errors << QStringLiteral( "%1/%2: 没有采集到帧统计" ).arg( env.name, video.label );
		if (result.failedFrames != 0)
			errors << QStringLiteral( "%1/%2: 失败帧 %3" )
			.arg( env.name, video.label ).arg( result.failedFrames );

		if (env.needsOverlay) {
			if (!result.hasStage( PerformanceStage::DisplayWait ))
				errors << QStringLiteral( "%1/%2: Overlay 没有消费到快照" ).arg( env.name, video.label );
			if (!result.hasStage( PerformanceStage::PresentLatency ))
				errors << QStringLiteral( "%1/%2: 没有采集到呈现延迟" ).arg( env.name, video.label );
			if (!result.hasStage( PerformanceStage::ConvertKernel ) && env.flowMode == FlowMode::GPU && env.nativeInterop)
				skipInterop = true;
			if (env.flowMode == FlowMode::GPU && !env.nativeInterop) {
				if (!result.hasStage( PerformanceStage::Frame2Cpu ))
					errors << QStringLiteral( "%1/%2: 互操作不可用但没有下载到 CPU" ).arg( env.name, video.label );
				if (result.stageCount( PerformanceStage::ConvertKernel ) != 0)
					errors << QStringLiteral( "%1/%2: 互操作不可用却出现了 ConvertKernel" ).arg( env.name, video.label );
			}
			if (env.flowMode == FlowMode::CPU && result.stageCount( PerformanceStage::ConvertKernel ) != 0)
				errors << QStringLiteral( "%1/%2: CPU 流程不应出现 ConvertKernel" ).arg( env.name, video.label );
			// 零拷贝 + 不保存路径不应下载到 CPU
			if (env.flowMode == FlowMode::GPU && env.nativeInterop && !env.save
				&& result.stageCount( PerformanceStage::Frame2Cpu ) != 0)
				errors << QStringLiteral( "%1/%2: 零拷贝路径仍然下载了 CPU 像素" ).arg( env.name, video.label );
			// 实时不保存不应做 CPU 渲染
			if (!env.save && result.stageCount( PerformanceStage::RenderImage ) != 0)
				errors << QStringLiteral( "%1/%2: 实时不保存却渲染了 CPU 图像" ).arg( env.name, video.label );
		}
		else {
			if (!result.hasStage( PerformanceStage::ToQImage ))
				errors << QStringLiteral( "%1/%2: 离线模式没有 QImage 转换" ).arg( env.name, video.label );
			if (result.stageCount( PerformanceStage::DisplayWait ) != 0)
				errors << QStringLiteral( "%1/%2: 离线模式不应有 DisplayWait" ).arg( env.name, video.label );
			if (result.stageCount( PerformanceStage::ConvertKernel ) != 0)
				errors << QStringLiteral( "%1/%2: 离线模式不应出现 ConvertKernel" ).arg( env.name, video.label );
			if (!result.offlineImageValid)
				errors << QStringLiteral( "%1/%2: 离线模式没有收到 QImage" ).arg( env.name, video.label );
		}

		if (env.save) {
			if (result.savedFrames == 0 || result.savedFileCount == 0)
				errors << QStringLiteral( "%1/%2: 开启保存但没有写出结果" ).arg( env.name, video.label );
			if (!result.hasStage( PerformanceStage::RenderImage ))
				errors << QStringLiteral( "%1/%2: 开启保存但没有 RenderImage" ).arg( env.name, video.label );
			if (!result.hasStage( PerformanceStage::Save ))
				errors << QStringLiteral( "%1/%2: 开启保存但没有 Save 阶段" ).arg( env.name, video.label );
		}
		else {
			if (result.savedFrames != 0 || result.savedFileCount != 0)
				errors << QStringLiteral( "%1/%2: 未开启保存却写出了结果" ).arg( env.name, video.label );
		}
	}

	printResult( env.name, results );

	if (skipInterop)
		QSKIP( "CUDA-D3D11 互操作未生效，零拷贝显示路径无法测量" );
	QVERIFY2( errors.isEmpty(), qPrintable( errors.join( '\n' ) ) );
}

double test_display_matrix::avgNsMs( quint64 totalNs, quint64 samples )
{
	if (samples == 0)
		return 0.0;
	return static_cast<double>( totalNs ) / static_cast<double>( samples ) / 1e6;
}

double test_display_matrix::stageAvgMs( const RunResult& result, PerformanceStage stage )
{
	const StageAgg& agg = result.stages[ static_cast<size_t>(stage) ];
	return avgNsMs( agg.totalNs, agg.count );
}

test_display_matrix::RunResult test_display_matrix::mergeRuns( const QVector<RunResult>& runs )
{
	RunResult merged;
	merged.resolution = QStringLiteral( "总体" );
	for (const RunResult& run : runs) {
		merged.recordedFrames += run.recordedFrames;
		merged.successFrames += run.successFrames;
		merged.failedFrames += run.failedFrames;
		merged.droppedFrames += run.droppedFrames;
		merged.savedFrames += run.savedFrames;
		merged.savedFileCount += run.savedFileCount;
		merged.savedDiskBytes += run.savedDiskBytes;
		merged.offlineImages += run.offlineImages;
		merged.objects += run.objects;
		merged.relayPosts += run.relayPosts;
		merged.overlaySnapshots += run.overlaySnapshots;
		merged.overlayRenders += run.overlayRenders;
		merged.windowMs += run.windowMs;
		merged.wallMs += run.wallMs;
		merged.narrowTotalNs += run.narrowTotalNs;
		merged.narrowSamples += run.narrowSamples;
		merged.broadTotalNs += run.broadTotalNs;
		merged.broadSamples += run.broadSamples;
		merged.warmupNs = std::max( merged.warmupNs, run.warmupNs );
		merged.warmupIterations = std::max( merged.warmupIterations, run.warmupIterations );
		for (size_t i = 0; i < PerformanceStageCount; ++i) {
			merged.stages[i].count += run.stages[i].count;
			merged.stages[i].totalNs += run.stages[i].totalNs;
			merged.stages[i].maxNs = std::max( merged.stages[i].maxNs, run.stages[i].maxNs );
		}
	}
	return merged;
}

void test_display_matrix::printStats( const QString& title, const RunResult& result )
{
	QTextStream out( stdout );
	// 整体帧率以墙钟为准；监控窗口只累计有帧的窗口，慢流程下会明显偏高，单独展示
	const double wallFps = result.wallMs > 0.0
		? static_cast<double>( result.successFrames ) * 1000.0 / result.wallMs : 0.0;
	const double windowFps = result.windowMs > 0.0
		? static_cast<double>( result.recordedFrames ) * 1000.0 / result.windowMs : 0.0;

	out << "\n[" << title << "]" << (result.resolution.isEmpty() ? QString() : QStringLiteral( " " ) + result.resolution) << "\n";
	out << "  记录帧: " << result.recordedFrames
		<< " | 成功: " << result.successFrames
		<< " | 失败: " << result.failedFrames
		<< " | 丢帧: " << result.droppedFrames
		<< " | 保存帧: " << result.savedFrames
		<< " | 保存文件: " << result.savedFileCount
		<< " | 离线QImage: " << result.offlineImages
		<< " | 目标数: " << result.objects
		<< " | relay: " << result.relayPosts
		<< " | setSnapshot: " << result.overlaySnapshots
		<< " | Overlay渲染: " << result.overlayRenders << "\n";
	out << "  整体帧率(墙钟): " << QString::number( wallFps, 'f', 2 )
		<< " FPS | 监控窗口帧率: " << QString::number( windowFps, 'f', 2 )
		<< " FPS | 统计窗口: " << QString::number( result.windowMs, 'f', 1 )
		<< " ms | 墙钟: " << QString::number( result.wallMs, 'f', 1 ) << " ms\n";
	out << "  狭义端到端: " << QString::number( avgNsMs( result.narrowTotalNs, result.narrowSamples ), 'f', 3 )
		<< " ms | 广义端到端: " << QString::number( avgNsMs( result.broadTotalNs, result.broadSamples ), 'f', 3 )
		<< " ms | 预热: " << QString::number( static_cast<double>( result.warmupNs ) / 1e6, 'f', 1 )
		<< " ms x " << result.warmupIterations << "\n";
	out << "  阶段\t平均(ms)\t最大(ms)\t样本数\n";
	for (int i = 0; i < static_cast<int>( PerformanceStageCount ); ++i) {
		const PerformanceStage stage = static_cast<PerformanceStage>( i );
		const StageAgg& agg = result.stages[ static_cast<size_t>(i) ];
		out << "  " << stageToString( stage ) << '\t'
			<< QString::number( stageAvgMs( result, stage ), 'f', 3 ) << '\t'
			<< QString::number( static_cast<double>( agg.maxNs ) / 1e6, 'f', 3 ) << '\t'
			<< agg.count << '\n';
	}
}

void test_display_matrix::printResult( const QString& scenario, const QVector<RunResult>& runs )
{
	QTextStream out( stdout );
	out << "\n========================================================================\n";
	out << "场景: " << scenario << "\n";
	out << "测试输出目录: " << m_workDir << "\n";
	out << "注: 整体FPS = 成功帧数 / 墙钟；窗口FPS 只累计有帧的监控窗口，慢流程下会偏高。\n";
	out << "注: 记录帧取任务结果；阶段样本数来自监控快照，窗口切换边界可能少 1~2 个样本。\n";
	out << "分辨率\t记录帧\t成功\t失败\t丢帧\t保存帧\t文件\tQImage\t目标\trelay\t渲染\t整体FPS\t窗口FPS\t狭义ms\t广义ms\t保存字节\n";
	auto printRow = [&out]( const RunResult& result ) {
		const double wallFps = result.wallMs > 0.0
			? static_cast<double>( result.successFrames ) * 1000.0 / result.wallMs : 0.0;
		const double windowFps = result.windowMs > 0.0
			? static_cast<double>( result.recordedFrames ) * 1000.0 / result.windowMs : 0.0;
		out << result.resolution << '\t' << result.recordedFrames << '\t'
			<< result.successFrames << '\t' << result.failedFrames << '\t'
			<< result.droppedFrames << '\t' << result.savedFrames << '\t'
			<< result.savedFileCount << '\t' << result.offlineImages << '\t'
			<< result.objects << '\t'
			<< result.relayPosts << '\t' << result.overlayRenders << '\t'
			<< QString::number( wallFps, 'f', 2 ) << '\t'
			<< QString::number( windowFps, 'f', 2 ) << '\t'
			<< QString::number( avgNsMs( result.narrowTotalNs, result.narrowSamples ), 'f', 3 ) << '\t'
			<< QString::number( avgNsMs( result.broadTotalNs, result.broadSamples ), 'f', 3 ) << '\t'
			<< result.savedDiskBytes << '\n';
	};

	for (const RunResult& result : runs)
		printRow( result );
	const RunResult overall = mergeRuns( runs );
	printRow( overall );

	for (const RunResult& result : runs)
		if (!result.saveDir.isEmpty())
			out << "  [" << result.resolution << "] 保存目录: " << result.saveDir << "\n";

	out << "\n阶段平均耗时(ms)\t";
	for (const RunResult& result : runs)
		out << result.resolution << '\t';
	out << "总体\n";
	for (int i = 0; i < static_cast<int>( PerformanceStageCount ); ++i) {
		const PerformanceStage stage = static_cast<PerformanceStage>( i );
		out << stageToString( stage ) << '\t';
		for (const RunResult& result : runs)
			out << QString::number( stageAvgMs( result, stage ), 'f', 3 ) << '\t';
		out << QString::number( stageAvgMs( overall, stage ), 'f', 3 ) << '\n';
	}

	printStats( QStringLiteral( "总体阶段明细" ), overall );
}

void test_display_matrix::test_realtime_gpuOk_noSave()
{
	runScenario( { QStringLiteral( "实时/GPU/互操作可用/不保存" ),
		ViewMode::Realtime, FlowMode::GPU, true, false, true } );
}

void test_display_matrix::test_realtime_gpuOk_Save()
{
	runScenario( { QStringLiteral( "实时/GPU/互操作可用/保存" ),
		ViewMode::Realtime, FlowMode::GPU, true, true, true } );
}

void test_display_matrix::test_realtime_gpuFail_noSave()
{
	runScenario( { QStringLiteral( "实时/GPU/互操作不可用/不保存" ),
		ViewMode::Realtime, FlowMode::GPU, false, false, true } );
}

void test_display_matrix::test_realtime_gpuFail_Save()
{
	runScenario( { QStringLiteral( "实时/GPU/互操作不可用/保存" ),
		ViewMode::Realtime, FlowMode::GPU, false, true, true } );
}

void test_display_matrix::test_offline_gpu_noSave()
{
	runScenario( { QStringLiteral( "离线/GPU/不保存" ),
		ViewMode::Offline, FlowMode::GPU, false, false, false } );
}

void test_display_matrix::test_offline_gpu_Save()
{
	runScenario( { QStringLiteral( "离线/GPU/保存" ),
		ViewMode::Offline, FlowMode::GPU, false, true, false } );
}

void test_display_matrix::test_offline_cpu_noSave()
{
	runScenario( { QStringLiteral( "离线/CPU/不保存" ),
		ViewMode::Offline, FlowMode::CPU, false, false, false } );
}

void test_display_matrix::test_offline_cpu_Save()
{
	runScenario( { QStringLiteral( "离线/CPU/保存" ),
		ViewMode::Offline, FlowMode::CPU, false, true, false } );
}

void test_display_matrix::test_realtime_cpu_noSave()
{
	runScenario( { QStringLiteral( "实时/CPU/不保存" ),
		ViewMode::Realtime, FlowMode::CPU, false, false, true } );
}

void test_display_matrix::test_realtime_cpu_Save()
{
	runScenario( { QStringLiteral( "实时/CPU/保存" ),
		ViewMode::Realtime, FlowMode::CPU, false, true, true } );
}

int main( int argc, char* argv[] )
{
	// 1280 模型 + 4K + CPU 流程单函数可能超过 QtTest 默认 5 分钟看门狗；
	// 在 QTest 启动前显式放大到 2 小时，整体超时由 CTest 的 TIMEOUT 兜底。
	qputenv( "QTEST_FUNCTION_TIMEOUT", "7200000" );

	QGuiApplication app( argc, argv );
	test_display_matrix test;
	return QTest::qExec( &test, argc, argv );
}

#include "test_display_matrix.moc"
