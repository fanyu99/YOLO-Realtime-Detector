#include "DetectionApp.h"
#include "PerformanceApp.h"
#include <QFileInfo>
#include <QCoreApplication>
#include <algorithm>
#include "AppConfigWriter.h"
#include "DetectionImageProvider.h"
#include "DetectionResultSaver.h"
#include "HikCameraFrameSource.h"
#include "FfmpegFrameSource.h"	
#include "Logger.h"

// 匿名工具函数: 帧信号通知限流
namespace {
	// 帧通知间隔，防止频繁发信号
	constexpr int kFrameNotifyIntervalMs = 16;

	// 判断是否重复，放置重复配置
	bool sameDetectorConfig ( const DetectorConfig& a, const DetectorConfig& b ) {
		return a.model_path == b.model_path
			&& a.class_names == b.class_names
			&& a.conf_threshold == b.conf_threshold
			&& a.iou_threshold == b.iou_threshold
			&& a.num_threads == b.num_threads
			&& a.device_id == b.device_id
			&& a.input_width == b.input_width
			&& a.input_height == b.input_height
			&& a.preferred_mode == b.preferred_mode;
	}

	QString yesNo ( bool value ) { return value ? QStringLiteral ( "是" ) : QStringLiteral ( "否" ); }
}

DetectionApp::DetectionApp ( QObject* parent ) : QObject ( parent ), m_filePath ( "" ), m_config{}
{
	Logger::instance ()->success ( QStringLiteral ( "[DetectionApp]: 应用已初始化" ) );

	m_frameMail = std::make_unique<FrameMail> ();
	m_manager = std::make_unique<DetectionManager> ( this );
	m_manager->transMailPtr ( m_frameMail.get () );	// 注入邮箱指针
	connect ( m_manager.get (), &DetectionManager::jobStarted, this, &DetectionApp::onJobStarted );
	connect ( m_manager.get (), &DetectionManager::jobProgress, this, &DetectionApp::onJobProgress );
	connect ( m_manager.get (), &DetectionManager::frameReady, this, &DetectionApp::onFrameReady );
	connect ( m_manager.get (), &DetectionManager::jobFinished, this, &DetectionApp::onJobFinished );
	connect ( m_manager.get (), &DetectionManager::jobFailed, this, &DetectionApp::onJobFailed );
	connect ( m_manager.get (), &DetectionManager::jobCancelled, this, &DetectionApp::onJobCancelled );
	connect ( m_manager.get (), &DetectionManager::detectorWarning, this, &DetectionApp::onDetectorWarning );
	connect ( m_manager.get (), &DetectionManager::jobSaved, this, &DetectionApp::onJobSaved );
	connect ( m_manager.get (), &DetectionManager::configAdjusted, this, &DetectionApp::onConfigAdjusted );
	connect ( m_manager.get (), &DetectionManager::fallbackOccurred, this, &DetectionApp::onFallbackOccurred );
	connect ( m_manager.get (), &DetectionManager::frameSnapshotPosted, this, &DetectionApp::onFrameSnapshotPosted );


}

DetectionApp* DetectionApp::initialize ( const DetectorConfig& config, const DisplayPolicy& displayPolicy, PerformanceApp* perfApp ) {
	if (m_instance) {
		m_instance->showBanner ( QStringLiteral ( "[DetectionApp]: 重复初始化，已忽略！" ), BannerLevel::BannerWarning );
		Logger::instance ()->warning ( QStringLiteral ( "[DetectionApp]: 重复初始化，已忽略！" ) );
	}
	else {
		m_instance.reset ( new DetectionApp () );
		m_instance->setPerformanceApp ( perfApp );
		m_instance->setInitialConfig ( config );
		m_instance->setInitialDisplayPolicy ( displayPolicy );	// 初始化用户模式
	}
	return m_instance.get ();
}

DetectionApp* DetectionApp::instance () {
	if (!m_instance)
		Logger::instance ()->fatal ( QStringLiteral ( "[DetectionApp]: 未初始化，必须先调用 initialize()!" ) );
	return m_instance.get ();
}

DetectionApp* DetectionApp::create ( QQmlEngine* qmlEngine, QJSEngine* jsEngine ) {
	Q_UNUSED ( jsEngine )
		DetectionApp* app = DetectionApp::instance ();
	if (!app)
		return nullptr;
	qmlEngine->setObjectOwnership ( app, QQmlEngine::CppOwnership );
	// 设置帧显示通道
	if (!app->m_frameProvider) {
		app->m_frameProvider = new DetectionImageProvider;
		qmlEngine->addImageProvider ( DetectionImageProvider::providerId (), app->m_frameProvider );
		Logger::instance ()->success ( QStringLiteral ( "[DetectionApp]: 帧显示通道已注册 (image://" )
			+ DetectionImageProvider::providerId () + QStringLiteral ( "/latest)" ) );
	}
	return app;
}

// 同步性能监测器的 CPU/GPU 模式
void DetectionApp::syncPerfMode () {
	if (m_performanceApp)
		m_performanceApp->setFlowModeTexts ( m_config.preferredModeText (), m_config.effectiveModeText () );
}

// 借用监测器，需要在初始化的时候进行注入
void DetectionApp::setPerformanceApp ( PerformanceApp* perfApp ) {
	m_performanceApp = perfApp;
	if (!perfApp) {
		m_manager->setMonitor ( nullptr );
		Logger::instance ()->fatal ( QStringLiteral ( "[DetectionApp]: performanceApp为空，监测器为空！" ) );
		return;
	}
	if (perfApp->monitor ()) {
		m_manager->setMonitor ( perfApp->monitor () );
		Logger::instance ()->info ( QStringLiteral ( "[DetectionApp]: 性能监测器已注入" ) );
	}
	else {
		Logger::instance ()->warning ( QStringLiteral ( "[DetectionApp]: 性能监测器不可用(监控级别为Off或类型无效)" ) );
	}
}

void DetectionApp::onFallbackOccurred ( const DetectionError& error ) {
	if (error.errorCode == DetectionErrorCode::FallbackRequested) {
		Logger::instance ()->warning ( QStringLiteral ( "[DetectionApp]: 检测器已触发 fallback 自愈，当前模式已切换为 CPU 模式" ) );
		m_config.active_mode = FlowMode::CPU;	// 自愈为 CPU 模式
		syncPerfMode (); // 同步性能监测器的 CPU/GPU 模式
		emit configChanged ();
	}
}

DetectionApp::~DetectionApp () {
	if (m_manager)
		m_manager->shutdown ();
}

// 统一上报错误：写日志(红色) + 通知 QML
void DetectionApp::reportError ( const AppError& error ) {
	Logger::instance ()->error ( error.errorMessage );
	emit errorOccurred ( error );
}

// 检测器错误 -> 应用层错误
AppError DetectionApp::mapDetectionErrortoAppError ( const DetectionError& error ) const {
	switch (error.errorCategory) {
	case DetectionErrorCategory::Model:		return AppError ( error.errorMessage, AppErrorCode::InvalidModelPath );
	case DetectionErrorCategory::Config:		return AppError ( error.errorMessage, AppErrorCode::InvalidConfig );
	case DetectionErrorCategory::Image:		return AppError ( error.errorMessage, AppErrorCode::InvalidFilePath );
	case DetectionErrorCategory::Detection:	return AppError ( error.errorMessage, AppErrorCode::DetectionFailed );
	case DetectionErrorCategory::Preprocess:return AppError ( error.errorMessage, AppErrorCode::DetectionFailed );
	default:								return AppError ( error.errorMessage, AppErrorCode::UnknownError );
	}
}

// 帧源描述构造:
// 由文件路径构造帧源描述
FrameSourceDescription DetectionApp::makeFileDescription ( QStringView path ) const {
	FrameSourceDescription des;
	des.type = isVideoPath ( path ) ? FrameSourceType::VideoFile : FrameSourceType::SingleImage;
	des.filePath = path.toString ();
	return des;
}

// 触发模式字符串 -> 枚举（无效值回退 Off 并记警告）
CameraTriggerMode DetectionApp::str2TriggerMode ( QStringView str ) const {
	if (str == u"software")	return CameraTriggerMode::Software;
	if (str == u"line0")	return CameraTriggerMode::Line0;
	if (str == u"line1")	return CameraTriggerMode::Line1;
	if (str == u"line2")	return CameraTriggerMode::Line2;
	if (str != u"off")
		Logger::instance ()->warning ( QStringLiteral ( "[DetectionApp]: 海康相机触发模式无效，使用默认的连续采集模式" ) );
	return CameraTriggerMode::Off;
}

// 枚举在线海康相机，供 HikCameraDialog 填列表(为阻塞操作)
QVariantList DetectionApp::hikDevices () const {
	QVariantList list;
	const QVector<HikCameraFrameSource::DeviceInfo> devices = HikCameraFrameSource::enumerateDevices ();
	list.reserve ( devices.size () );
	for (const HikCameraFrameSource::DeviceInfo& device : devices) {
		QVariantMap item;
		item.insert ( QStringLiteral ( "serial" ), device.serial );
		item.insert ( QStringLiteral ( "model" ), device.model );
		item.insert ( QStringLiteral ( "userDefinedName" ), device.userDefinedName );
		item.insert ( QStringLiteral ( "online" ), device.online );
		list.append ( item );
	}
	return list;
}

// 枚举 ffmpeg video 输入设备(摄像头等)，供选择对话框填列表(为阻塞操作)
QVariantList DetectionApp::ffmpegVideoDevices ( const QString& formatName ) const {
	QVariantList list;
	const FfmpegDeviceInfoList devices = FfmpegFrameSource::enumerateVideoDevices ( formatName );
	list.reserve ( devices.size () );
	for (const FfmpegDeviceInfo& device : devices) {
		QVariantMap item;
		item.insert ( QStringLiteral ( "name" ), device.name );		// 友好名：列表显示 + 创建帧源都用它
		item.insert ( QStringLiteral ( "id" ), device.originalId );			// 原始标识（PNP 路径），仅排查用
		item.insert ( QStringLiteral ( "isDefault" ), device.isDefault );
		list.append ( item );
	}
	return list;
}

// 配置摘要(用于日志)
QString DetectionApp::configSummary () const {
	return QStringLiteral ( "模型: " ) + QString::fromStdWString ( m_config.model_path.wstring () )
		+ QStringLiteral ( " 类别数: " ) + QString::number ( m_config.class_names.size () )
		+ QStringLiteral ( " 置信度: " ) + QString::number ( m_config.conf_threshold )
		+ QStringLiteral ( " NMS: " ) + QString::number ( m_config.iou_threshold )
		+ QStringLiteral ( " 输入: " ) + QString::number ( m_config.input_width )
		+ QStringLiteral ( "x" ) + QString::number ( m_config.input_height )
		+ QStringLiteral ( " 线程: " ) + QString::number ( m_config.num_threads )
		+ QStringLiteral ( " 设备: " ) + QString::number ( m_config.device_id );
}

void DetectionApp::setBusy ( bool busy ) {
	if (m_busy == busy)
		return;
	m_busy = busy;
	emit busyChanged ();
}

// qml侧调用: 检测
void DetectionApp::onDetectClicked () {
	if (m_busy) {
		reportError ( AppError ( QStringLiteral ( "[DetectionApp]: 已有检测在进行中，请先停止!" ), AppErrorCode::DetectionFailed ) );
		return;
	}
	if (m_filePath.empty ()) {
		reportError ( AppError ( QStringLiteral ( "[Detector]: 文件路径为空!" ), AppErrorCode::InvalidFilePath ) );
		return;
	}
	if (!std::filesystem::exists ( m_filePath )) {
		reportError ( AppError ( QStringLiteral ( "[Detector]: 文件路径无效!" ), AppErrorCode::InvalidFilePath ) );
		return;
	}
	if (!m_config.isValid ()) {
		reportError ( AppError ( QStringLiteral ( "[Detector]: 配置无效!" ), AppErrorCode::InvalidConfig ) );
		return;
	}
	if (!m_config.model_exists ()) {
		reportError ( AppError ( QStringLiteral ( "[Detector]: 模型路径无效!" ), AppErrorCode::InvalidModelPath ) );
		return;
	}
	if (!m_manager->start ()) {
		reportError ( AppError ( QStringLiteral ( "[Detector]: 检测器未就绪!" ), AppErrorCode::DetectorError ) );
		return;
	}

	const QString path = QString::fromStdWString ( m_filePath.wstring () );
	if (!isImagePath ( path ) && !isVideoPath ( path )) {
		reportError ( AppError ( QStringLiteral ( "[Detector]: 文件类型不支持!" ), AppErrorCode::InvalidFilePath ) );
		return;
	}
	// 单图在GPU模式下于Session层上传显存后走GPU流程
	Logger::instance ()->info ( QStringLiteral ( "[DetectionApp]: 提交文件检测: " ) + path );
	m_currentJobId = m_manager->submitSource ( makeFileDescription ( path ), makeDisplaySaveStatus () );

	if (m_currentJobId == 0)
		return;						// 提交失败，Manager 已经 emit jobFailed

	m_progress = 0.0f;
	setState ( m_currentJobId, false, true );
}

// 获取示例资源根目录
QString DetectionApp::samplesRoot () const {
	return QCoreApplication::applicationDirPath () + QStringLiteral ( "/examples" );
}

// qml侧调用: 一键示例检测。
void DetectionApp::runSampleDetection () {
	const QString root = samplesRoot ();
	if (!QFileInfo::exists ( root )) {
		reportError ( AppError ( QStringLiteral ( "[DetectionApp]: 示例资源目录不存在: " ) + root, AppErrorCode::InvalidFilePath ) );
		return;
	}

	// 示例文件路径
	const QUrl sampleImage = QUrl::fromLocalFile ( root + QStringLiteral ( "/images/1 (369).jpg" ) );
	const QUrl sampleModel = QUrl::fromLocalFile ( root + QStringLiteral ( "/models/african-wildlife_best.onnx" ) );

	// 类别名
	const QStringList sampleClasses{ QStringLiteral ( "buffalo" ), QStringLiteral ( "elephant" ),
									 QStringLiteral ( "rhino" ), QStringLiteral ( "zebra" ) };

	// 示例配置
	DetectorConfig sample = m_config;
	sample.model_path = std::filesystem::path ( sampleModel.toLocalFile ().toStdWString () );
	if (sample.class_names.isEmpty ())
		sample.class_names = sampleClasses;
	if (sample.conf_threshold <= 0.0f || sample.conf_threshold >= 1.0f)
		sample.conf_threshold = 0.25f;
	if (sample.iou_threshold <= 0.0f || sample.iou_threshold >= 1.0f)
		sample.iou_threshold = 0.45f;

	setFilePath ( sampleImage );
	setConfig ( sample );			// 应用成功才提交
	if (m_config.model_path != sample.model_path || !m_config.isValid ()) {
		Logger::instance ()->warning ( QStringLiteral ( "[DetectionApp]: 示例配置未能应用，已取消示例检测" ) );
		return;
	}
	Logger::instance ()->info ( QStringLiteral ( "[DetectionApp]: 示例检测: " ) + sampleImage.toLocalFile () );
	// 进行检测
	onDetectClicked ();
}

// qml侧调用: 海康相机检测
void DetectionApp::onDetectHikCameraClicked ( const QString& serial, const QString& triggerMode ) {
	if (m_busy) {
		reportError ( AppError ( QStringLiteral ( "[DetectionApp]: 已有检测在进行中，请先停止!" ), AppErrorCode::DetectionFailed ) );
	}
	if (!m_config.isValid ()) {
		reportError ( AppError ( QStringLiteral ( "[DetectionApp]: 配置无效!" ), AppErrorCode::InvalidConfig ) );
		return;
	}
	if (!m_config.model_exists ()) {
		reportError ( AppError ( QStringLiteral ( "[DetectionApp]: 模型路径无效!" ), AppErrorCode::InvalidModelPath ) );
		return;
	}
	if (!m_manager->start ()) {
		reportError ( AppError ( QStringLiteral ( "[DetectionApp]: 检测器未就绪!" ), AppErrorCode::DetectorError ) );
		return;
	}

	Logger::instance ()->info ( QStringLiteral ( "[DetectionApp]: 请求海康相机检测，设备序列号 = " ) + serial + QStringLiteral ( ", 触发模式 = " ) + triggerMode );
	if (serial.isEmpty ()) {
		reportError ( AppError ( QStringLiteral ( "[DetectionApp]: 海康相机设备序列号无效!" ), AppErrorCode::InvalidConfig ) );
		return;
	}
	// 构造帧源描述并提交（帧源由 worker 执行时经 Session 创建）
	FrameSourceDescription des;
	des.type = FrameSourceType::Hik;
	des.serial = serial;
	des.triggerMode = str2TriggerMode ( triggerMode );
	m_currentJobId = m_manager->submitSource ( des, makeDisplaySaveStatus () );
	if (m_currentJobId == 0)
		return;

	m_progress = 0.0f;
	setState ( m_currentJobId, true, true );
}
// qml侧调用: Ffmpeg设备检测（摄像头，dshow/avfoundation/v4l2）
void DetectionApp::onDetectFfmpegDeviceClicked ( const QString& deviceName, const QString& formatName ) {
	if (m_busy) {
		reportError ( AppError ( QStringLiteral ( "[DetectionApp]: 已有检测在进行中，请先停止!" ), AppErrorCode::DetectionFailed ) );
		return;
	}
	if (deviceName.isEmpty ()) {
		reportError ( AppError ( QStringLiteral ( "[DetectionApp]: 设备名为空!" ), AppErrorCode::InvalidConfig ) );
		return;
	}
	if (!m_config.isValid ()) {
		reportError ( AppError ( QStringLiteral ( "[DetectionApp]: 配置无效!" ), AppErrorCode::InvalidConfig ) );
		return;
	}
	if (!m_config.model_exists ()) {
		reportError ( AppError ( QStringLiteral ( "[DetectionApp]: 模型路径无效!" ), AppErrorCode::InvalidModelPath ) );
		return;
	}
	if (!m_manager->start ()) {
		reportError ( AppError ( QStringLiteral ( "[DetectionApp]: 检测器未就绪!" ), AppErrorCode::DetectorError ) );
		return;
	}
	Logger::instance ()->info ( QStringLiteral ( "[DetectionApp]: 请求 Ffmpeg 设备检测，设备 = " ) + deviceName
		+ QStringLiteral ( ", 格式 = " ) + (formatName.isEmpty () ? FfmpegFrameSource::defaultVideoInputFormat () : formatName) );
	// 构造帧源描述并提交（设备是否在线由 worker 执行时校验，失败经 taskFailed 上报）
	FrameSourceDescription des;
	des.type = FrameSourceType::Camera;
	des.nameOrId = deviceName;
	des.formatName = formatName;
	m_currentJobId = m_manager->submitSource ( des, makeDisplaySaveStatus () );
	if (m_currentJobId == 0)
		return;
	m_progress = 0.0f;
	setState ( m_currentJobId, true, true );
}

// qml侧调用: 批量检测（图片/视频可混排，一个 Job 含多个 Task）
quint64 DetectionApp::submitBatch ( const QList<QUrl>& urls ) {
	QStringList paths;
	paths.reserve ( urls.size () );
	for (const QUrl& url : urls) {
		const QString path = url.toLocalFile ();
		if (!path.isEmpty ())
			paths.append ( path );
	}

	if (m_busy) {
		reportError ( AppError ( QStringLiteral ( "[DetectionApp]: 已有检测在进行中，请先停止!" ), AppErrorCode::DetectionFailed ) );
		return 0;
	}
	if (paths.isEmpty ()) {
		reportError ( AppError ( QStringLiteral ( "[DetectionApp]: 批量输入为空!" ), AppErrorCode::InvalidFilePath ) );
		return 0;
	}
	if (!m_config.isValid ()) {
		reportError ( AppError ( QStringLiteral ( "[Detector]: 配置无效!" ), AppErrorCode::InvalidConfig ) );
		return 0;
	}
	if (!m_config.model_exists ()) {
		reportError ( AppError ( QStringLiteral ( "[Detector]: 模型路径无效!" ), AppErrorCode::InvalidModelPath ) );
		return 0;
	}
	if (!m_manager->start ()) {
		reportError ( AppError ( QStringLiteral ( "[Detector]: 检测器未就绪!" ), AppErrorCode::DetectorError ) );
		return 0;
	}

	Logger::instance ()->info ( QStringLiteral ( "[DetectionApp]: 提交批量检测，文件数: " )
		+ QString::number ( paths.size () ) );

	// 构造帧源描述。认不出的后缀归为单图，由 worker 执行时报"图片加载失败"，
	// 与逐个提交时的行为一致。
	QVector<FrameSourceDescription> descriptions;
	descriptions.reserve ( paths.size () );
	for (const QString& path : paths)
		descriptions.append ( makeFileDescription ( path ) );


	m_currentJobId = m_manager->submitBatch ( std::move ( descriptions ), makeDisplaySaveStatus () );
	if (m_currentJobId == 0)
		return 0;								// 提交失败，Manager 已经 emit jobFailed

	m_progress = 0.0f;
	setState ( m_currentJobId, false, true );
	return m_currentJobId;
}

// qml侧调用: 停止当前检测
void DetectionApp::cancelCurrentJob () {
	if (m_currentJobId == 0) {
		Logger::instance ()->info ( QStringLiteral ( "[DetectionApp]: 当前没有正在执行的检测" ) );
		return;
	}
	if (!m_manager->cancel ( m_currentJobId )) {
		Logger::instance ()->warning ( QStringLiteral ( "[DetectionApp]: 取消失败，作业可能已经结束" ) );
		return;
	}
	Logger::instance ()->info ( QStringLiteral ( "[DetectionApp]: 已请求停止当前检测" ) );
}

// qml侧调用: 关闭警告横幅
void DetectionApp::dismissBanner () {
	if (m_bannerText.isEmpty ())
		return;
	m_bannerText.clear ();
	m_bannerLevel = BannerNone;
	emit bannerChanged ();
}

// 统一的横幅出口：一条槽位，避免多条堆叠
void DetectionApp::showBanner ( QStringView text, BannerLevel level ) {
	m_bannerText = text.toString ();
	m_bannerLevel = level;
	emit bannerChanged ();
}

QString DetectionApp::frameSource () const {
	// 回看模式优先：画面切到缓冲里的那一帧
	if (m_reviewIndex >= 0 && m_reviewIndex < m_review.size ())
		return DetectionImageProvider::resultUrl ( m_review[m_reviewIndex].seq );
	return DetectionImageProvider::sourceUrl ( m_frameRevision );
}

// 回看：只动 UI 侧的索引，不碰 worker
void DetectionApp::nextResult () {
	if (m_review.isEmpty ())
		return;
	m_reviewIndex = (m_reviewIndex < 0) ? 0 : std::min ( m_reviewIndex + 1, static_cast<int> ( m_review.size () ) - 1 );
	++m_frameRevision;				// 让 QML 的 source 绑定重新求值
	emit frameRevisionChanged ();
	emit reviewChanged ();
}

void DetectionApp::prevResult () {
	if (m_review.isEmpty ())
		return;
	m_reviewIndex = (m_reviewIndex < 0) ? 0 : std::max ( 0, m_reviewIndex - 1 );
	++m_frameRevision;
	emit frameRevisionChanged ();
	emit reviewChanged ();
}

void DetectionApp::followLatest () {
	if (m_reviewIndex < 0)
		return;
	m_reviewIndex = -1;
	++m_frameRevision;
	emit frameRevisionChanged ();
	emit reviewChanged ();
}

QString DetectionApp::resultInfo () const {
	if (m_review.isEmpty ())
		return QStringLiteral ( "暂无结果" );
	if (m_reviewIndex < 0)
		return QStringLiteral ( "实时 · 已缓存 %1 个结果" ).arg ( m_review.size () );

	const ReviewItem& item = m_review[m_reviewIndex];
	return QStringLiteral ( "第 %1/%2 · %3 · %4 个目标 · %5 ms" )
		.arg ( m_reviewIndex + 1 ).arg ( m_review.size () )
		.arg ( item.name ).arg ( item.objectCount ).arg ( item.durationMs );
}

void DetectionApp::resetReview () {
	m_review.clear ();
	m_reviewIndex = -1;
	if (m_frameProvider)
		m_frameProvider->clearResultFrames ();
	emit reviewChanged ();
	emit frameRevisionChanged ();
}

QUrl DetectionApp::saveDir () const {
	return m_saveDir.isEmpty () ? QUrl () : QUrl::fromLocalFile ( m_saveDir );
}

// qml侧调用: 设置"保存推理结果"开关与目录
void DetectionApp::setSaveOptions ( bool enabled, const QUrl& dir ) {
	const QString path = dir.isEmpty () ? QString () : dir.toLocalFile ();

	if (enabled && path.isEmpty ()) {
		reportError ( AppError ( QStringLiteral ( "[DetectionApp]: 勾选了保存结果但未指定保存路径!" ), AppErrorCode::InvalidFilePath ) );
		return;
	}

	m_saveEnabled = enabled;
	m_saveDir = path;
	Logger::instance ()->info ( enabled
		? QStringLiteral ( "[DetectionApp]: 已开启结果保存 -> " ) + path + QStringLiteral ( "/detection_results" )
		: QStringLiteral ( "[DetectionApp]: 已关闭结果保存" ) );
	emit saveOptionsChanged ();
}

// Getter
QList<ClassColorItem> DetectionApp::clsClrInfo () const {
	return m_classColorItems;
}
QUrl DetectionApp::modelPath () const {
	// std::filesystem::path 在 Windows 上是宽字符，用 wstring 转换可正确处理中文路径
	return QUrl::fromLocalFile ( QString::fromStdWString ( m_config.model_path.wstring () ) );
}

QUrl DetectionApp::filePath () const {
	return QUrl::fromLocalFile ( QString::fromStdWString ( m_filePath.wstring () ) );
}
DetectorConfig DetectionApp::config ()const {
	return m_config;
}
QStringList DetectionApp::classNames () const {
	return m_config.class_names;
}

// 设置相关配置
void DetectionApp::setFilePath ( const QUrl& path ) {
	this->m_filePath = std::filesystem::path ( path.toLocalFile ().toStdWString () );

	// 清除前面的显示帧
	m_frameRevision = 0;
	resetReview ();
	if (m_overlay)
		m_overlay->resetFrames ();
	if (m_frameProvider)
		m_frameProvider->setFrame ( QImage () );
	emit frameRevisionChanged ();

	Logger::instance ()->info ( QStringLiteral ( "[DetectionApp]: 已选择文件: " ) + path.toLocalFile () );
	emit filePathChanged ();
}

void DetectionApp::setConfig ( const DetectorConfig& config ) {
	applyDetectorConfig ( config, true );
}

// 应用配置
void DetectionApp::applyDetectorConfig ( const DetectorConfig& config, bool persist ) {
	// 运行中不允许改配置：换了模型需要重建 ONNX 会话，而重建意味着必须先停
	if (m_busy) {
		reportError ( AppError ( QStringLiteral ( "[DetectionApp]: 检测进行中，无法修改配置，请先停止!" ), AppErrorCode::InvalidConfig ) );
		return;
	}
	if (!config.isValid ()) {
		reportError ( AppError ( QStringLiteral ( "[Detector]: 配置无效!" ), AppErrorCode::InvalidConfig ) );
		return;
	}
	if (!config.model_exists ()) {
		reportError ( AppError ( QStringLiteral ( "[Detector]: 模型路径无效!" ), AppErrorCode::InvalidModelPath ) );
		return;
	}

	// 判断是否重复
	const bool configDiffers = !sameDetectorConfig ( m_config, config );

	// manager应用成功后再本地保存
	if (!m_manager->applyConfig ( config )) {
		reportError ( AppError ( QStringLiteral ( "[DetectionApp]: 配置未能应用（配置无效或检测进行中）!" ), AppErrorCode::InvalidConfig ) );
		return;
	}
	// 应用成功后再进行App更改
	const bool pathChanged = (m_config.model_path != config.model_path);
	const bool clsChanged = (m_config.class_names != config.class_names);

	this->m_config = config;
	syncPerfMode ();  // 同步性能监测器的 CPU/GPU 模式
	if (pathChanged)
		emit modelPathChanged ();
	if (clsChanged)
		setClsClrInfo ( m_config.classNamesText () );
	emit configChanged ();		// 纯通知：让 QML 侧边栏刷新
	Logger::instance ()->info ( QStringLiteral ( "[DetectionApp]: 检测器已就绪 -> " ) + configSummary () );

	// 配置保存失败进行提醒上报
	if (!persist || !configDiffers)
		return;
	AppConfigWriter* writer = AppConfigWriter::instance ();
	if (!writer->writeDetectorConfig ( config ))
		reportError ( AppError ( QStringLiteral ( "[DetectionApp]: 配置已生效，但写入配置文件失败: " )
			+ writer->errorMessage (), AppErrorCode::ConfigWriteFailed ) );
}

void DetectionApp::setConfig ( const QUrl& model_path, const QStringList& class_names, float conf_confidence, float iou_threshold, int num_threads, int device_id, int input_width, int input_height, const QString& flow_mode ) {
	DetectorConfig config;
	config.model_path = std::filesystem::path ( model_path.toLocalFile ().toStdWString () );
	config.class_names = class_names;
	config.conf_threshold = conf_confidence;
	config.iou_threshold = iou_threshold;
	config.num_threads = num_threads;
	config.device_id = device_id;
	config.input_width = input_width;
	config.input_height = input_height;
	auto  newMode = str2FlowMode ( flow_mode );
	if (!newMode.has_value ()) {
		// 无效保留当前模式
		config.preferred_mode = m_config.preferred_mode;
		config.active_mode = m_config.active_mode;
		onDetectorWarning ( DetectorWarning ( QStringLiteral ( "[DetectionApp]: 无效的流模式: " ) + flow_mode, DetectorWarning::Code::InvalidFlowMode ) );
	}
	else {
		config.preferred_mode = newMode.value ();
		config.active_mode = std::nullopt;
	}
	setConfig ( config );
}

// 设置默认配置
void DetectionApp::setInitialConfig ( const DetectorConfig& config ) {
	// 初始化应用配置
	applyDetectorConfig ( config, false );
}

// 初始化显示模式
void DetectionApp::setInitialDisplayPolicy ( const DisplayPolicy& policy ) {
	m_displayPolicy = policy;
	Logger::instance ()->info ( QStringLiteral ( "[DetectionApp]: 预览模式(用户) -> " ) + preferredViewMode () );
}

// 设置预览模式：应用成功后再落盘
void DetectionApp::setViewMode ( const QString& viewMode ) {
	const auto mode = String2ViewMode ( viewMode );
	if (!mode) {
		reportError ( AppError ( QStringLiteral ( "[DetectionApp]: 无效的预览模式: " ) + viewMode, AppErrorCode::InvalidConfig ) );
		return;
	}
	applyViewMode ( *mode, true );
}

// 应用预览模式
void DetectionApp::applyViewMode ( const ViewMode& mode, bool persist ) {
	if (!m_displayPolicy.setViewMode ( mode ))
		return;
	emit preferredViewModeChanged ();
	emit activeViewModeChanged ();
	// 切换后重置Overlay的帧,qml进行刷新使用ImageProvider
	if (m_overlay)
		m_overlay->resetFrames ();
	Logger::instance ()->info ( QStringLiteral ( "[DetectionApp]: 预览模式已切换 -> " ) + ViewMode2String ( mode ) );

	// 配置保存失败进行提醒上报
	if (!persist)
		return;
	AppConfigWriter* writer = AppConfigWriter::instance ();
	if (!writer->writeDisplayConfig ( m_displayPolicy ))
		reportError ( AppError ( QStringLiteral ( "[DetectionApp]: 模式已生效，但写入配置文件失败: " )
			+ writer->errorMessage (), AppErrorCode::ConfigWriteFailed ) );
}

// 推导显示/保存状态
DisplaySaveStatus DetectionApp::makeDisplaySaveStatus () {
	const bool gpuFlow = (m_config.effectiveMode () == FlowMode::GPU);
	const bool overlayAvailable = (m_overlay != nullptr); // 叠加层是否可用
	const DisplaySaveStatus status = DisplaySaveStatus::getDisplaySaveStatus (
		m_displayPolicy.preferredMode (), gpuFlow, overlayAvailable, effectiveResultsDir () );

	// 回填实际生效的模式、后端
	const bool modeChanged = (m_displayPolicy.activeMode () != status.activeMode);
	m_displayPolicy.setActiveMode ( status.activeMode );
	m_displayPolicy.setBackend ( status.backend );
	if (modeChanged)
		emit activeViewModeChanged ();

	if (status.activeMode != m_displayPolicy.preferredMode ())
	{
		Logger::instance ()->warning ( QStringLiteral ( "[DetectionApp]: 场景中没有叠加层，实时预览已降级为图片显示" ) );
		showBanner ( QStringLiteral ( "[DetectionApp]: 场景中没有叠加层，实时预览已降级为图片显示！" ), BannerLevel::BannerWarning );
	}

	Logger::instance ()->info ( QStringLiteral ( "[DetectionApp]: 显示需求：后端=%1 烧框=%2 QImage=%3 快照=%4 保存=%5 下载=%6" )
		.arg ( Backend2String ( status.backend ), yesNo ( status.renderBoxes ),
			yesNo ( status.needQImage ), yesNo ( status.needSnapshot ),
			yesNo ( status.isSaveResult () ), yesNo ( status.download2CPU ) ) );
	// 根据当前的模式以及是否保存进行横幅警告提示
	if (status.isSaveResult () && status.activeMode == ViewMode::Realtime)
	{
		Logger::instance ()->warning ( QStringLiteral ( "[DetectionApp]: 实时检测结果将被保存" ) );
		showBanner ( QStringLiteral ( "[DetectionApp]: 实时检测结果将被保存，性能会有所下降！" ), BannerLevel::BannerWarning );
	}
	return status;
}

// 设置模式
void DetectionApp::setFlowMode ( const QString& flow_mode ) {
	auto  newMode = str2FlowMode ( flow_mode );
	if (!newMode.has_value ()) {
		// 无效模式，忽略并警告
		onDetectorWarning ( DetectorWarning ( QStringLiteral ( "[DetectionApp]: 无效的流模式: " ) + flow_mode, DetectorWarning::Code::InvalidFlowMode ) );
		return;
	}
	if (m_config.effectiveMode () == newMode.value ())
		return;						// 没变就不改了
	DetectorConfig next = m_config;
	next.preferred_mode = newMode.value ();
	next.active_mode = std::nullopt;
	setConfig ( next );
}
// 获取当前实际生效的模式
QString DetectionApp::effectiveFlowMode ()const {
	return m_config.effectiveModeText ();
}

void DetectionApp::setModelPath ( const QUrl& path ) {
	if (!std::filesystem::exists ( std::filesystem::path ( path.toLocalFile ().toStdWString () ) )) {
		reportError ( AppError ( QStringLiteral ( "[Detector]: 模型路径无效!" ), AppErrorCode::InvalidModelPath ) );
		return;
	}

	DetectorConfig next = m_config;
	next.model_path = std::filesystem::path ( path.toLocalFile ().toStdWString () );

	if (next.isValid ()) {
		setConfig ( next );			// 配置已完整：正常应用 + 通知
		return;
	}

	m_config = next;
	Logger::instance ()->info ( QStringLiteral ( "[DetectionApp]: 模型路径已设置(配置未完整，暂不下发): " ) + path.toLocalFile () );
	emit modelPathChanged ();
}

// Manager 的作业事件 -> App 的 QML 接口

void DetectionApp::onJobStarted ( quint64 jobId ) {
	if (jobId != m_currentJobId)
		return;
	resetReview ();
	if (m_overlay)
		m_overlay->resetFrames ();
	setBusy ( true );
}

void DetectionApp::onJobProgress ( quint64 jobId, float progress ) {
	if (jobId != m_currentJobId)
		return;
	m_progress = progress;
	m_progressIndeterminate = m_manager->indeterminate ( jobId );
	emit progressChanged ();
}

// 将字符串变为模式枚举
std::optional<FlowMode> DetectionApp::str2FlowMode ( QStringView str )const {
	QMetaEnum meta = QMetaEnum::fromType<FlowMode> ();
	bool valid = false;
	int mode = meta.keyToValue ( str.toUtf8 ().constData (), &valid );
	return valid ? std::make_optional ( static_cast<FlowMode>(mode) ) : std::nullopt;
}

// 叠加层自注册/反注册
void DetectionApp::installOverlay ( DetectionOverlayItem* overlay ) {
	m_overlay = overlay;
	// 设置是否叠加层的互操作可用
	if (m_frameMail)
		m_frameMail->setNativeOk ( overlay != nullptr );
}

void DetectionApp::uninstallOverlay ( DetectionOverlayItem* overlay ) {
	if (m_overlay == overlay)
	{
		m_frameMail->setNativeOk ( false );
		m_overlay = nullptr;
	}
}

// 借用监测器，Overlay获取监测器进行计时
IPerformanceMonitor* DetectionApp::monitor () const {
	return m_performanceApp ? m_performanceApp->monitor () : nullptr;
}

void DetectionApp::onFrameSnapshotPosted () {
	if (!m_overlay)
		return;
	// 替换丢帧数
	const quint64 dropped = m_frameMail->dropped ();
	if (dropped != m_lastReportedDropped) {
		if (auto* mon = monitor ())
			for (quint64 i = m_lastReportedDropped; i < dropped; ++i)
				mon->recordStage ( PerformanceStage::DisplayDropped, 0 );	// 累加
		m_lastReportedDropped = dropped;
	}
	if (auto snap = m_frameMail->take ()) {
		m_overlay->setSnapshot ( std::move ( snap ) );
	}
}

void DetectionApp::onFrameReady ( quint64 jobId, const DetectionResult& result, const QImage& frame ) {
	if (jobId != m_currentJobId)
		return;

	// 在实时模式下不进行回看
	if (!m_frameProvider || m_displayPolicy.displayBackend () != DisplayBackend::ImageProvider
		|| frame.isNull ())
		return;

	// 仅在非实时模式下使用回看缓冲
	m_frameProvider->setFrame ( frame );		// 实时帧永远更新到最新

	// 追加到回看缓冲。有界环形，追加不会阻塞推理 ——
	// 这正是"不让 worker 等 UI 点下一个"的落点。
	ReviewItem item;
	item.seq = m_frameProvider->appendResultFrame ( frame );
	item.name = result.source.isEmpty ()
		? QStringLiteral ( "(摄像头)" )
		: QFileInfo ( result.source ).fileName ();
	item.objectCount = static_cast<int>(result.boxes.size ());
	item.durationMs = result.duration;

	const int dropped = m_review.size () + 1 - DetectionImageProvider::kMaxReviewFrames;
	m_review.append ( item );
	if (dropped > 0) {
		m_review.remove ( 0, dropped );
		// 缓冲头部被裁掉，回看索引要跟着左移，否则会指到错条目上
		if (m_reviewIndex >= 0)
			m_reviewIndex = std::max ( 0, m_reviewIndex - dropped );
	}
	emit reviewChanged ();

	// 回看模式下不跟随实时：否则每来一帧都会把画面重新加载成回看图，白费算力
	if (m_reviewIndex >= 0)
		return;

	// 通知 QML 要限流：30fps 逐帧通知会把 GUI 线程压满
	// 这里控制信号发出的频率
	if (!m_signalLimitTimer.isValid () || m_signalLimitTimer.elapsed () >= kFrameNotifyIntervalMs) {
		m_signalLimitTimer.restart ();
		++m_frameRevision;
		emit frameRevisionChanged ();
	}
}

void DetectionApp::onJobFinished ( quint64 jobId, DetectionJobType type,
	const QVector<DetectionResult>& results,
	int totalTasks, int failedTasks ) {
	if (jobId != m_currentJobId)
		return;

	// 补帧
	++m_frameRevision;
	emit frameRevisionChanged ();

	const int succeeded = totalTasks - failedTasks;

	auto SucBanner = [ this ] ( QStringView info ) {
		showBanner ( info, BannerSuccess );
		};
	// 种类通过manager进行回传
	QString info;
	switch (type) {
	case DetectionJobType::Batch:
		// 批量只报汇总
		info = QStringLiteral ( "[DetectionApp]: 批量检测完成，成功 %1 / 失败 %2（共 %3）" )
			.arg ( succeeded ).arg ( failedTasks ).arg ( totalTasks );
		Logger::instance ()->success ( info );
		SucBanner ( info );
		emit batchFinished ( jobId, totalTasks, succeeded, failedTasks );
		break;

	case DetectionJobType::SingleVideo:
	case DetectionJobType::Camera:
		info = QStringLiteral ( "[DetectionApp]: 视频检测完成，帧数: " ) + QString::number ( results.size () );
		Logger::instance ()->success ( info );
		SucBanner ( info );
		emit videoDetectionFinished ( results );
		break;

	case DetectionJobType::SingleImage:
		if (!results.isEmpty ()) {
			const DetectionResult& result = results.first ();
			info = QStringLiteral ( "[DetectionApp]: 图片检测完成，目标数: " )
				+ QString::number ( result.boxes.size () )
				+ QStringLiteral ( "，耗时: " ) + QString::number ( result.duration ) + QStringLiteral ( "ms" );
			Logger::instance ()->success ( info );
			SucBanner ( info );
			emit imageDetectionFinished ( result );
		}
		break;
	}

	m_progress = 1.0f;
	setState ( 0, false, false );
}

void DetectionApp::onJobFailed ( quint64 jobId, const DetectionError& error ) {
	// 这里不能过滤JobId，manager内部失败时没有同步JobId
	// 发送错误
	reportError ( mapDetectionErrortoAppError ( error ) );

	// 清理状态
	if (m_currentJobId == 0 || jobId == m_currentJobId) {
		m_currentJobId = 0;
		m_progressIndeterminate = false;
		if (m_performanceApp)
			m_performanceApp->setCurrentJobId ( 0 );
		setBusy ( false );
		emit progressChanged ();
	}
}

void DetectionApp::onJobCancelled ( quint64 jobId ) {
	if (jobId != m_currentJobId)
		return;

	Logger::instance ()->warning ( QStringLiteral ( "[DetectionApp]: 作业已取消" ) );
	// 清理状态
	setState ( 0, false, false );
}

// 检测器警告(非致命)：日志已在 worker 侧记录，这里负责把它送到 UI 显式提示

void DetectionApp::onDetectorWarning ( const DetectorWarning& warning ) {
	showBanner ( warning.message, BannerWarning );
}

// 结果已落盘：成功横幅（路径 + 文件数）。日志侧 worker 已经记过一条。
void DetectionApp::onJobSaved ( quint64 jobId, QStringView dir, int fileCount ) {
	Q_UNUSED ( jobId );
	showBanner ( QStringLiteral ( "已保存 %1 个推理结果到 %2" ).arg ( fileCount ).arg ( dir ), BannerSuccess );
}

// 检测器按模型对齐配置尺寸，自愈
void DetectionApp::onConfigAdjusted ( const DetectorConfig& config ) {
	this->m_config = config;
	syncPerfMode ();  // 同步性能监测器的 CPU/GPU 模式
	emit configChanged ();
	Logger::instance ()->info ( QStringLiteral ( "[DetectionApp]: 配置已按模型对齐 -> " ) + configSummary () );
}

void DetectionApp::setState ( quint64 currentJobId, bool progressInterminate, bool busy ) {
	m_currentJobId = currentJobId;
	m_progressIndeterminate = progressInterminate;
	if (m_performanceApp)
	{
		m_performanceApp->setCurrentJobId ( currentJobId );
		m_performanceApp->setIsRunning ( busy );	  // 在有任务在跑的时候才进行快照发送
	}
	setBusy ( busy );
	emit progressChanged ();
}

void DetectionApp::setClsClrInfo ( const QStringList& clsNames )
{
	m_classColorItems = ClassPalette::getClass_ColorInfo ( clsNames );
	emit clsClrInfoChanged ();
}