#include "DetectionApp.h"

#include <QFileInfo>
#include <QCoreApplication>
#include <QQmlEngine>
#include <QVariantMap>
#include <algorithm>

#include "DetectionImageProvider.h"
#include "DetectionResultSaver.h"
#include "HikCameraFrameSource.h"
#include "FfmpegFrameSource.h"	
#include "Logger.h"

// 匿名工具函数: 帧信号通知限流
namespace {
	// 帧通知的最小间隔：视频/摄像头 30fps 时逐帧通知 QML 会压满 GUI 线程，
	// 按 ~60fps 封顶。帧本身仍然每次都更新，只是信号"通知"被限流。
	constexpr int kFrameNotifyIntervalMs = 16;
}

DetectionApp::DetectionApp( QObject* parent ) : QObject( parent ), m_filePath( "" ), m_config{}
{
	Logger::instance()->info( QStringLiteral( "[DetectionApp]: 应用已初始化" ) );

	m_manager = std::make_unique<DetectionManager>( this );
	connect( m_manager.get(), &DetectionManager::jobStarted, this, &DetectionApp::onJobStarted );
	connect( m_manager.get(), &DetectionManager::jobProgress, this, &DetectionApp::onJobProgress );
	connect( m_manager.get(), &DetectionManager::frameReady, this, &DetectionApp::onFrameReady );
	connect( m_manager.get(), &DetectionManager::jobFinished, this, &DetectionApp::onJobFinished );
	connect( m_manager.get(), &DetectionManager::jobFailed, this, &DetectionApp::onJobFailed );
	connect( m_manager.get(), &DetectionManager::jobCancelled, this, &DetectionApp::onJobCancelled );
	connect( m_manager.get(), &DetectionManager::detectorWarning, this, &DetectionApp::onDetectorWarning );
	connect( m_manager.get(), &DetectionManager::jobSaved, this, &DetectionApp::onJobSaved );
	connect( m_manager.get(), &DetectionManager::configAdjusted, this, &DetectionApp::onConfigAdjusted );
	connect( m_manager.get(), &DetectionManager::fallbackOccurred, this, &DetectionApp::onFallbackOccurred );


}

void DetectionApp::onFallbackOccurred( const DetectionError& error ) {
	if (error.errorCode == DetectionErrorCode::FallbackRequested) {
		Logger::instance()->warning( QStringLiteral( "[DetectionApp]: 检测器已触发 fallback 自愈，当前模式已切换为 CPU 模式" ) );
		m_config.active_mode = FlowMode::CPU;	// 自愈为 CPU 模式
		emit configChanged();
	}
}

DetectionApp::~DetectionApp() {
	if (m_manager)
		m_manager->shutdown();
}

// 帧显示通道：注册 image provider（"image://detection/latest?rev=N"）
void DetectionApp::componentComplete() {
	QQmlEngine* engine = qmlEngine( this );
	if (!engine) {
		Logger::instance()->warning( QStringLiteral( "[DetectionApp]: 未关联 QML engine，帧显示通道不可用" ) );
		return;
	}

	// 所有权交给 engine（它会负责释放），这里只留一个观察用裸指针
	m_frameProvider = new DetectionImageProvider;
	engine->addImageProvider( DetectionImageProvider::providerId(), m_frameProvider );

	Logger::instance()->info( QStringLiteral( "[DetectionApp]: 帧显示通道已注册 (image://" )
		+ DetectionImageProvider::providerId() + QStringLiteral( "/latest)" ) );
}

// 统一上报错误：写日志(红色) + 通知 QML
void DetectionApp::reportError( const AppError& error ) {
	Logger::instance()->error( error.errorMessage );
	emit errorOccurred( error );
}

// 检测器错误 -> 应用层错误
AppError DetectionApp::mapDetectionErrortoAppError( const DetectionError& error ) const {
	switch (error.errorCategory) {
	case DetectionErrorCategory::Model:		return AppError( error.errorMessage, AppErrorCode::InvalidModelPath );
	case DetectionErrorCategory::Config:		return AppError( error.errorMessage, AppErrorCode::InvalidConfig );
	case DetectionErrorCategory::Image:		return AppError( error.errorMessage, AppErrorCode::InvalidFilePath );
	case DetectionErrorCategory::Detection:	return AppError( error.errorMessage, AppErrorCode::DetectionFailed );
	case DetectionErrorCategory::Preprocess:return AppError( error.errorMessage, AppErrorCode::DetectionFailed );
	default:								return AppError( error.errorMessage, AppErrorCode::UnknownError );
	}
}

// 帧源描述构造:
// 由文件路径构造帧源描述
FrameSourceDescription DetectionApp::makeFileDescription( QStringView path ) const {
	FrameSourceDescription des;
	des.type = isVideoPath( path ) ? FrameSourceType::VideoFile : FrameSourceType::SingleImage;
	des.filePath = path.toString();
	return des;
}

// 触发模式字符串 -> 枚举（无效值回退 Off 并记警告）
CameraTriggerMode DetectionApp::str2TriggerMode( QStringView str ) const {
	if (str == u"software")	return CameraTriggerMode::Software;
	if (str == u"line0")	return CameraTriggerMode::Line0;
	if (str == u"line1")	return CameraTriggerMode::Line1;
	if (str == u"line2")	return CameraTriggerMode::Line2;
	if (str != u"off")
		Logger::instance()->warning( QStringLiteral( "[DetectionApp]: 海康相机触发模式无效，使用默认的连续采集模式" ) );
	return CameraTriggerMode::Off;
}

// 枚举在线海康相机，供 HikCameraDialog 填列表(为阻塞操作)
QVariantList DetectionApp::hikDevices() const {
	QVariantList list;
	const QVector<HikCameraFrameSource::DeviceInfo> devices = HikCameraFrameSource::enumerateDevices();
	list.reserve( devices.size() );
	for (const HikCameraFrameSource::DeviceInfo& device : devices) {
		QVariantMap item;
		item.insert( QStringLiteral( "serial" ), device.serial );
		item.insert( QStringLiteral( "model" ), device.model );
		item.insert( QStringLiteral( "userDefinedName" ), device.userDefinedName );
		item.insert( QStringLiteral( "online" ), device.online );
		list.append( item );
	}
	return list;
}

// 枚举 ffmpeg video 输入设备(摄像头等)，供选择对话框填列表(为阻塞操作)
QVariantList DetectionApp::ffmpegVideoDevices( const QString& formatName ) const {
	QVariantList list;
	const FfmpegDeviceInfoList devices = FfmpegFrameSource::enumerateVideoDevices( formatName );
	list.reserve( devices.size() );
	for (const FfmpegDeviceInfo& device : devices) {
		QVariantMap item;
		item.insert( QStringLiteral( "name" ), device.name );		// 友好名：列表显示 + 创建帧源都用它
		item.insert( QStringLiteral( "id" ), device.originalId );			// 原始标识（PNP 路径），仅排查用
		item.insert( QStringLiteral( "isDefault" ), device.isDefault );
		list.append( item );
	}
	return list;
}

// 配置摘要(用于日志)
QString DetectionApp::configSummary() const {
	return QStringLiteral( "模型: " ) + QString::fromStdWString( m_config.model_path.wstring() )
		+ QStringLiteral( " 类别数: " ) + QString::number( m_config.class_names.size() )
		+ QStringLiteral( " 置信度: " ) + QString::number( m_config.conf_threshold )
		+ QStringLiteral( " NMS: " ) + QString::number( m_config.iou_threshold )
		+ QStringLiteral( " 输入: " ) + QString::number( m_config.input_width )
		+ QStringLiteral( "x" ) + QString::number( m_config.input_height )
		+ QStringLiteral( " 线程: " ) + QString::number( m_config.num_threads )
		+ QStringLiteral( " 设备: " ) + QString::number( m_config.device_id );
}

void DetectionApp::setBusy( bool busy ) {
	if (m_busy == busy)
		return;
	m_busy = busy;
	emit busyChanged();
}

// qml侧调用: 检测
void DetectionApp::onDetectClicked() {
	if (m_busy) {
		reportError( AppError( QStringLiteral( "[DetectionApp]: 已有检测在进行中，请先停止!" ), AppErrorCode::DetectionFailed ) );
		return;
	}
	if (m_filePath.empty()) {
		reportError( AppError( QStringLiteral( "[Detector]: 文件路径为空!" ), AppErrorCode::InvalidFilePath ) );
		return;
	}
	if (!std::filesystem::exists( m_filePath )) {
		reportError( AppError( QStringLiteral( "[Detector]: 文件路径无效!" ), AppErrorCode::InvalidFilePath ) );
		return;
	}
	if (!m_config.isValid()) {
		reportError( AppError( QStringLiteral( "[Detector]: 配置无效!" ), AppErrorCode::InvalidConfig ) );
		return;
	}
	if (!m_config.model_exists()) {
		reportError( AppError( QStringLiteral( "[Detector]: 模型路径无效!" ), AppErrorCode::InvalidModelPath ) );
		return;
	}
	if (!m_manager->start()) {
		reportError( AppError( QStringLiteral( "[Detector]: 检测器未就绪!" ), AppErrorCode::DetectorError ) );
		return;
	}

	const QString path = QString::fromStdWString( m_filePath.wstring() );
	if (!isImagePath( path ) && !isVideoPath( path )) {
		reportError( AppError( QStringLiteral( "[Detector]: 文件类型不支持!" ), AppErrorCode::InvalidFilePath ) );
		return;
	}
	// 单图在GPU模式下于Session层上传显存后走GPU流程
	Logger::instance()->info( QStringLiteral( "[DetectionApp]: 提交文件检测: " ) + path );
	m_currentJobId = m_manager->submitSource( makeFileDescription( path ), effectiveResultsDir() );

	if (m_currentJobId == 0)
		return;						// 提交失败，Manager 已经 emit jobFailed

	m_progress = 0.0f;
	setState( m_currentJobId, false, true );
}

// 获取示例资源根目录
QString DetectionApp::samplesRoot() const {
	return QCoreApplication::applicationDirPath() + QStringLiteral( "/examples" );
}

// qml侧调用: 一键示例检测。
void DetectionApp::runSampleDetection() {
	const QString root = samplesRoot();
	if (!QFileInfo::exists( root )) {
		reportError( AppError( QStringLiteral( "[DetectionApp]: 示例资源目录不存在: " ) + root, AppErrorCode::InvalidFilePath ) );
		return;
	}

	// 示例文件路径
	const QUrl sampleImage = QUrl::fromLocalFile( root + QStringLiteral( "/images/1 (369).jpg" ) );
	const QUrl sampleModel = QUrl::fromLocalFile( root + QStringLiteral( "/models/african-wildlife_best.onnx" ) );

	// 类别名
	const QStringList sampleClasses{ QStringLiteral( "buffalo" ), QStringLiteral( "elephant" ),
									 QStringLiteral( "rhino" ), QStringLiteral( "zebra" ) };

	// 示例配置
	DetectorConfig sample = m_config;
	sample.model_path = std::filesystem::path( sampleModel.toLocalFile().toStdWString() );
	if (sample.class_names.isEmpty())
		sample.class_names = sampleClasses;
	if (sample.conf_threshold <= 0.0f || sample.conf_threshold >= 1.0f)
		sample.conf_threshold = 0.25f;
	if (sample.iou_threshold <= 0.0f || sample.iou_threshold >= 1.0f)
		sample.iou_threshold = 0.45f;

	setFilePath( sampleImage );
	setConfig( sample );			// 应用成功才提交
	if (m_config.model_path != sample.model_path || !m_config.isValid()) {
		Logger::instance()->warning( QStringLiteral( "[DetectionApp]: 示例配置未能应用，已取消示例检测" ) );
		return;
	}
	Logger::instance()->info( QStringLiteral( "[DetectionApp]: 示例检测: " ) + sampleImage.toLocalFile() );
	// 进行检测
	onDetectClicked();
}

// qml侧调用: 海康相机检测
void DetectionApp::onDetectHikCameraClicked( const QString& serial, const QString& triggerMode ) {
	if (m_busy) {
		reportError( AppError( QStringLiteral( "[DetectionApp]: 已有检测在进行中，请先停止!" ), AppErrorCode::DetectionFailed ) );
	}
	if (!m_config.isValid()) {
		reportError( AppError( QStringLiteral( "[DetectionApp]: 配置无效!" ), AppErrorCode::InvalidConfig ) );
		return;
	}
	if (!m_config.model_exists()) {
		reportError( AppError( QStringLiteral( "[DetectionApp]: 模型路径无效!" ), AppErrorCode::InvalidModelPath ) );
		return;
	}
	if (!m_manager->start()) {
		reportError( AppError( QStringLiteral( "[DetectionApp]: 检测器未就绪!" ), AppErrorCode::DetectorError ) );
		return;
	}

	Logger::instance()->info( QStringLiteral( "[DetectionApp]: 请求海康相机检测，设备序列号 = " ) + serial + QStringLiteral( ", 触发模式 = " ) + triggerMode );
	if (serial.isEmpty()) {
		reportError( AppError( QStringLiteral( "[DetectionApp]: 海康相机设备序列号无效!" ), AppErrorCode::InvalidConfig ) );
		return;
	}
	// 构造帧源描述并提交（帧源由 worker 执行时经 Session 创建）
	FrameSourceDescription des;
	des.type = FrameSourceType::Hik;
	des.serial = serial;
	des.triggerMode = str2TriggerMode( triggerMode );
	m_currentJobId = m_manager->submitSource( des, effectiveResultsDir() );
	if (m_currentJobId == 0)
		return;

	m_progress = 0.0f;
	setState( m_currentJobId, true, true );
}
// qml侧调用: Ffmpeg设备检测（摄像头，dshow/avfoundation/v4l2）
void DetectionApp::onDetectFfmpegDeviceClicked( const QString& deviceName, const QString& formatName ) {
	if (m_busy) {
		reportError( AppError( QStringLiteral( "[DetectionApp]: 已有检测在进行中，请先停止!" ), AppErrorCode::DetectionFailed ) );
		return;
	}
	if (deviceName.isEmpty()) {
		reportError( AppError( QStringLiteral( "[DetectionApp]: 设备名为空!" ), AppErrorCode::InvalidConfig ) );
		return;
	}
	if (!m_config.isValid()) {
		reportError( AppError( QStringLiteral( "[DetectionApp]: 配置无效!" ), AppErrorCode::InvalidConfig ) );
		return;
	}
	if (!m_config.model_exists()) {
		reportError( AppError( QStringLiteral( "[DetectionApp]: 模型路径无效!" ), AppErrorCode::InvalidModelPath ) );
		return;
	}
	if (!m_manager->start()) {
		reportError( AppError( QStringLiteral( "[DetectionApp]: 检测器未就绪!" ), AppErrorCode::DetectorError ) );
		return;
	}
	Logger::instance()->info( QStringLiteral( "[DetectionApp]: 请求 Ffmpeg 设备检测，设备 = " ) + deviceName
		+ QStringLiteral( ", 格式 = " ) + (formatName.isEmpty() ? FfmpegFrameSource::defaultVideoInputFormat() : formatName) );
	// 构造帧源描述并提交（设备是否在线由 worker 执行时校验，失败经 taskFailed 上报）
	FrameSourceDescription des;
	des.type = FrameSourceType::Camera;
	des.nameOrId = deviceName;
	des.formatName = formatName;
	m_currentJobId = m_manager->submitSource( des, effectiveResultsDir() );
	if (m_currentJobId == 0)
		return;
	m_progress = 0.0f;
	setState( m_currentJobId, true, true );
}

// qml侧调用: 批量检测（图片/视频可混排，一个 Job 含多个 Task）
quint64 DetectionApp::submitBatch( const QList<QUrl>& urls ) {
	QStringList paths;
	paths.reserve( urls.size() );
	for (const QUrl& url : urls) {
		const QString path = url.toLocalFile();
		if (!path.isEmpty())
			paths.append( path );
	}

	if (m_busy) {
		reportError( AppError( QStringLiteral( "[DetectionApp]: 已有检测在进行中，请先停止!" ), AppErrorCode::DetectionFailed ) );
		return 0;
	}
	if (paths.isEmpty()) {
		reportError( AppError( QStringLiteral( "[DetectionApp]: 批量输入为空!" ), AppErrorCode::InvalidFilePath ) );
		return 0;
	}
	if (!m_config.isValid()) {
		reportError( AppError( QStringLiteral( "[Detector]: 配置无效!" ), AppErrorCode::InvalidConfig ) );
		return 0;
	}
	if (!m_config.model_exists()) {
		reportError( AppError( QStringLiteral( "[Detector]: 模型路径无效!" ), AppErrorCode::InvalidModelPath ) );
		return 0;
	}
	if (!m_manager->start()) {
		reportError( AppError( QStringLiteral( "[Detector]: 检测器未就绪!" ), AppErrorCode::DetectorError ) );
		return 0;
	}

	Logger::instance()->info( QStringLiteral( "[DetectionApp]: 提交批量检测，文件数: " )
		+ QString::number( paths.size() ) );

	// 构造帧源描述。认不出的后缀归为单图，由 worker 执行时报"图片加载失败"，
	// 与逐个提交时的行为一致。
	QVector<FrameSourceDescription> descriptions;
	descriptions.reserve( paths.size() );
	for (const QString& path : paths)
		descriptions.append( makeFileDescription( path ) );


	m_currentJobId = m_manager->submitBatch( std::move( descriptions ), effectiveResultsDir() );
	if (m_currentJobId == 0)
		return 0;								// 提交失败，Manager 已经 emit jobFailed

	m_progress = 0.0f;
	setState( m_currentJobId, false, true );
	return m_currentJobId;
}

// qml侧调用: 停止当前检测
void DetectionApp::cancelCurrentJob() {
	if (m_currentJobId == 0) {
		Logger::instance()->info( QStringLiteral( "[DetectionApp]: 当前没有正在执行的检测" ) );
		return;
	}
	if (!m_manager->cancel( m_currentJobId )) {
		Logger::instance()->warning( QStringLiteral( "[DetectionApp]: 取消失败，作业可能已经结束" ) );
		return;
	}
	Logger::instance()->info( QStringLiteral( "[DetectionApp]: 已请求停止当前检测" ) );
}

// qml侧调用: 关闭警告横幅
void DetectionApp::dismissBanner() {
	if (m_bannerText.isEmpty())
		return;
	m_bannerText.clear();
	m_bannerLevel = BannerNone;
	emit bannerChanged();
}

// 统一的横幅出口：一条槽位，避免多条堆叠
void DetectionApp::showBanner( QStringView text, BannerLevel level ) {
	m_bannerText = text.toString();
	m_bannerLevel = level;
	emit bannerChanged();
}

QString DetectionApp::frameSource() const {
	// 回看模式优先：画面切到缓冲里的那一帧
	if (m_reviewIndex >= 0 && m_reviewIndex < m_review.size())
		return DetectionImageProvider::resultUrl( m_review[m_reviewIndex].seq );
	return DetectionImageProvider::sourceUrl( m_frameRevision );
}

// 回看：只动 UI 侧的索引，不碰 worker
void DetectionApp::nextResult() {
	if (m_review.isEmpty())
		return;
	m_reviewIndex = (m_reviewIndex < 0) ? 0 : std::min( m_reviewIndex + 1, static_cast<int>( m_review.size() ) - 1 );
	++m_frameRevision;				// 让 QML 的 source 绑定重新求值
	emit frameRevisionChanged();
	emit reviewChanged();
}

void DetectionApp::prevResult() {
	if (m_review.isEmpty())
		return;
	m_reviewIndex = (m_reviewIndex < 0) ? 0 : std::max( 0, m_reviewIndex - 1 );
	++m_frameRevision;
	emit frameRevisionChanged();
	emit reviewChanged();
}

void DetectionApp::followLatest() {
	if (m_reviewIndex < 0)
		return;
	m_reviewIndex = -1;
	++m_frameRevision;
	emit frameRevisionChanged();
	emit reviewChanged();
}

QString DetectionApp::resultInfo() const {
	if (m_review.isEmpty())
		return QStringLiteral( "暂无结果" );
	if (m_reviewIndex < 0)
		return QStringLiteral( "实时 · 已缓存 %1 个结果" ).arg( m_review.size() );

	const ReviewItem& item = m_review[m_reviewIndex];
	return QStringLiteral( "第 %1/%2 · %3 · %4 个目标 · %5 ms" )
		.arg( m_reviewIndex + 1 ).arg( m_review.size() )
		.arg( item.name ).arg( item.objectCount ).arg( item.durationMs );
}

void DetectionApp::resetReview() {
	m_review.clear();
	m_reviewIndex = -1;
	if (m_frameProvider)
		m_frameProvider->clearResultFrames();
	emit reviewChanged();
}

QUrl DetectionApp::saveDir() const {
	return m_saveDir.isEmpty() ? QUrl() : QUrl::fromLocalFile( m_saveDir );
}

// qml侧调用: 设置"保存推理结果"开关与目录
void DetectionApp::setSaveOptions( bool enabled, const QUrl& dir ) {
	const QString path = dir.isEmpty() ? QString() : dir.toLocalFile();

	if (enabled && path.isEmpty()) {
		reportError( AppError( QStringLiteral( "[DetectionApp]: 勾选了保存结果但未指定保存路径!" ), AppErrorCode::InvalidFilePath ) );
		return;
	}

	m_saveEnabled = enabled;
	m_saveDir = path;
	Logger::instance()->info( enabled
		? QStringLiteral( "[DetectionApp]: 已开启结果保存 -> " ) + path + QStringLiteral( "/detection_results" )
		: QStringLiteral( "[DetectionApp]: 已关闭结果保存" ) );
	emit saveOptionsChanged();
}

// Getter
QUrl DetectionApp::modelPath() const {
	// std::filesystem::path 在 Windows 上是宽字符，用 wstring 转换可正确处理中文路径
	return QUrl::fromLocalFile( QString::fromStdWString( m_config.model_path.wstring() ) );
}

QUrl DetectionApp::filePath() const {
	return QUrl::fromLocalFile( QString::fromStdWString( m_filePath.wstring() ) );
}
DetectorConfig DetectionApp::config()const {
	return m_config;
}
QStringList DetectionApp::classNames() const {
	return m_config.class_names;
}

// 设置相关配置
void DetectionApp::setFilePath( const QUrl& path ) {
	this->m_filePath = std::filesystem::path( path.toLocalFile().toStdWString() );

	// 换了文件就把上一轮的检测画面清掉：QML 用 frameRevision > 0 判断
	// "是否已有检测结果"，不清的话会一直显示上一个文件的画面
	m_frameRevision = 0;
	resetReview();
	if (m_frameProvider)
		m_frameProvider->setFrame( QImage() );
	emit frameRevisionChanged();

	Logger::instance()->info( QStringLiteral( "[DetectionApp]: 已选择文件: " ) + path.toLocalFile() );
	emit filePathChanged();
}

void DetectionApp::setConfig( const DetectorConfig& config ) {
	// 运行中不允许改配置：换了模型需要重建 ONNX 会话，而重建意味着必须先停
	if (m_busy) {
		reportError( AppError( QStringLiteral( "[DetectionApp]: 检测进行中，无法修改配置，请先停止!" ), AppErrorCode::InvalidConfig ) );
		return;
	}
	if (!config.isValid()) {
		reportError( AppError( QStringLiteral( "[Detector]: 配置无效!" ), AppErrorCode::InvalidConfig ) );
		return;
	}
	if (!config.model_exists()) {
		reportError( AppError( QStringLiteral( "[Detector]: 模型路径无效!" ), AppErrorCode::InvalidModelPath ) );
		return;
	}

	// 先让 Manager 应用，成功了才提交本地配置。
	// 这样 App 的 m_config 和 Manager 的 m_config 不会分叉 ——
	// 反过来写（先改本地再通知）会出现"本地已改、执行层没改"的静默不一致。
	if (!m_manager->applyConfig( config )) {
		reportError( AppError( QStringLiteral( "[DetectionApp]: 配置未能应用（配置无效或检测进行中）!" ), AppErrorCode::InvalidConfig ) );
		return;
	}
	// 应用成功后再进行App更改,App仅持有配置副本
	const bool pathChanged = (m_config.model_path != config.model_path);

	this->m_config = config;
	if (pathChanged)
		emit modelPathChanged();
	emit configChanged();		// 纯通知：让 QML 侧边栏刷新
	Logger::instance()->info( QStringLiteral( "[DetectionApp]: 检测器已就绪 -> " ) + configSummary() );
}

void DetectionApp::setConfig( const QUrl& model_path, const QStringList& class_names, float conf_confidence, float iou_threshold, int num_threads, int device_id, int input_width, int input_height,const QString& flow_mode ) {
	DetectorConfig config;
	config.model_path = std::filesystem::path( model_path.toLocalFile().toStdWString() );
	config.class_names = class_names;
	config.conf_threshold = conf_confidence;
	config.iou_threshold = iou_threshold;
	config.num_threads = num_threads;
	config.device_id = device_id;
	config.input_width = input_width;
	config.input_height = input_height;
	auto  newMode = str2FlowMode( flow_mode );
	if (!newMode.has_value()) {
		// 无效模式，忽略并警告
		onDetectorWarning( DetectorWarning( QStringLiteral( "[DetectionApp]: 无效的流模式: " ) + flow_mode , DetectorWarning::Code::InvalidFlowMode ) );
	 }
	// 懒设置，只有与当前实际生效的模式不同时才进行更改
	else if (m_config.effectiveMode() != newMode.value()) {
		config.preferred_mode = newMode.value();
		config.active_mode = std::nullopt;
	}
	setConfig( config );
}

// 设置模式
void DetectionApp::setFlowMode( const QString& flow_mode ) {
	auto  newMode = str2FlowMode( flow_mode );
	if (!newMode.has_value()) {
		// 无效模式，忽略并警告
		onDetectorWarning( DetectorWarning( QStringLiteral( "[DetectionApp]: 无效的流模式: " ) + flow_mode, DetectorWarning::Code::InvalidFlowMode ) );
		return;
	}
	if (m_config.effectiveMode() == newMode.value())
		return;						// 没变就不改了
	DetectorConfig next = m_config;
	next.preferred_mode = newMode.value();
	next.active_mode = std::nullopt;
	setConfig( next );
}
// 获取当前实际生效的模式
QString DetectionApp::effectiveFlowMode()const {
	return m_config.effectiveModeText();
}

void DetectionApp::setModelPath( const QUrl& path ) {
	if (!std::filesystem::exists( std::filesystem::path( path.toLocalFile().toStdWString() ) )) {
		reportError( AppError( QStringLiteral( "[Detector]: 模型路径无效!" ), AppErrorCode::InvalidModelPath ) );
		return;
	}

	// modelPath 也是配置的一部分：改了就构造一份新配置，走 setConfig 的同一条路径，
	// 保证"应用成功才提交"这条不变式只在一处实现，
	// 也避免 App 的 m_config 和 Manager 的 m_config 悄悄分叉
	DetectorConfig next = m_config;
	next.model_path = std::filesystem::path( path.toLocalFile().toStdWString() );

	if (next.isValid()) {
		setConfig( next );			// 配置已完整：正常应用 + 通知
		return;
	}

	// 配置还没填完（例如 class_names 为空）：先记下路径，等配置补全后再下发
	m_config = next;
	Logger::instance()->info( QStringLiteral( "[DetectionApp]: 模型路径已设置(配置未完整，暂不下发): " ) + path.toLocalFile() );
	emit modelPathChanged();
}

// Manager 的作业事件 -> App 的 QML 接口

void DetectionApp::onJobStarted( quint64 jobId ) {
	if (jobId != m_currentJobId)
		return;
	resetReview();				// 新作业从干净的回看缓冲开始
	setBusy( true );
}

void DetectionApp::onJobProgress( quint64 jobId, float progress ) {
	if (jobId != m_currentJobId)
		return;
	m_progress = progress;
	m_progressIndeterminate = m_manager->indeterminate( jobId );
	emit progressChanged();
}

// 将字符串变为模式枚举
std::optional<FlowMode> DetectionApp::str2FlowMode( QStringView str )const {
	QMetaEnum meta = QMetaEnum::fromType<FlowMode>();
	bool valid = false;
	int mode = meta.keyToValue( str.toUtf8().constData(), &valid );
	return valid ? std::make_optional(static_cast<FlowMode>(mode)) : std::nullopt;
}

void DetectionApp::onFrameReady( quint64 jobId, const DetectionResult& result, const QImage& frame ) {
	if (jobId != m_currentJobId || frame.isNull())
		return;

	if (m_frameProvider) {
		m_frameProvider->setFrame( frame );		// 实时帧永远更新到最新

		// 追加到回看缓冲。有界环形，追加不会阻塞推理 ——
		// 这正是"不让 worker 等 UI 点下一个"的落点。
		ReviewItem item;
		item.seq = m_frameProvider->appendResultFrame( frame );
		item.name = result.source.isEmpty()
			? QStringLiteral( "(摄像头)" )
			: QFileInfo( result.source ).fileName();
		item.objectCount = static_cast<int>(result.boxes.size());
		item.durationMs = result.duration;

		const int dropped = m_review.size() + 1 - DetectionImageProvider::kMaxReviewFrames;
		m_review.append( item );
		if (dropped > 0) {
			m_review.remove( 0, dropped );
			// 缓冲头部被裁掉，回看索引要跟着左移，否则会指到错条目上
			if (m_reviewIndex >= 0)
				m_reviewIndex = std::max( 0, m_reviewIndex - dropped );
		}
		emit reviewChanged();
	}

	// 回看模式下不跟随实时：否则每来一帧都会把画面重新加载成回看图，白费算力
	if (m_reviewIndex >= 0)
		return;

	// 通知 QML 要限流：30fps 逐帧通知会把 GUI 线程压满
	// 这里控制信号发出的频率
	if (!m_frameThrottle.isValid() || m_frameThrottle.elapsed() >= kFrameNotifyIntervalMs) {
		m_frameThrottle.restart();
		++m_frameRevision;
		emit frameRevisionChanged();
	}
}

void DetectionApp::onJobFinished( quint64 jobId, DetectionJobType type,
	const QVector<DetectionResult>& results,
	int totalTasks, int failedTasks ) {
	if (jobId != m_currentJobId)
		return;

	// 限流可能把最后一帧的通知吞掉，这里补一次，保证画面停在结果上
	++m_frameRevision;
	emit frameRevisionChanged();

	const int succeeded = totalTasks - failedTasks;

	// 种类由 Manager 回传，这里不需要任何跨事件维护的状态，纯分支
	switch (type) {
	case DetectionJobType::Batch:
		// 批量：结果不在 results 里（视频帧会把它撑爆），只报汇总
		Logger::instance()->success( QStringLiteral( "[DetectionApp]: 批量检测完成，成功 %1 / 失败 %2（共 %3）" )
			.arg( succeeded ).arg( failedTasks ).arg( totalTasks ) );
		emit batchFinished( jobId, totalTasks, succeeded, failedTasks );
		break;

	case DetectionJobType::SingleVideo:
	case DetectionJobType::Camera:
		Logger::instance()->success( QStringLiteral( "[DetectionApp]: 视频检测完成，帧数: " ) + QString::number( results.size() ) );
		emit videoDetectionFinished( results );
		break;

	case DetectionJobType::SingleImage:
		if (!results.isEmpty()) {
			const DetectionResult& result = results.first();
			Logger::instance()->success( QStringLiteral( "[DetectionApp]: 图片检测完成，目标数: " )
				+ QString::number( result.boxes.size() )
				+ QStringLiteral( "，耗时: " ) + QString::number( result.duration ) + QStringLiteral( "ms" ) );
			emit imageDetectionFinished( result );
		}
		break;
	}

	m_progress = 1.0f;
	setState( 0, false, false );
}

void DetectionApp::onJobFailed( quint64 jobId, const DetectionError& error ) {
	// 注意：投递失败时 Manager 会在 submit() 内部就同步 emit jobFailed，
	// 此时 m_currentJobId 还没被赋值。所以这里不能按 jobId 过滤掉，
	// 否则错误会被静默吞掉。
	reportError( mapDetectionErrortoAppError( error ) );

	// 清理状态
	if (m_currentJobId == 0 || jobId == m_currentJobId) {
		m_currentJobId = 0;
		m_progressIndeterminate = false;
		setBusy( false );
		emit progressChanged();
	}
}

void DetectionApp::onJobCancelled( quint64 jobId ) {
	if (jobId != m_currentJobId)
		return;

	Logger::instance()->warning( QStringLiteral( "[DetectionApp]: 作业已取消" ) );
	// 清理状态
	setState( 0, false, false );
}

// 检测器警告(非致命)：日志已在 worker 侧记录，这里负责把它送到 UI 显式提示

void DetectionApp::onDetectorWarning( const DetectorWarning& warning ) {
	showBanner( warning.message, BannerWarning );
}

// 结果已落盘：成功横幅（路径 + 文件数）。日志侧 worker 已经记过一条。
void DetectionApp::onJobSaved( quint64 jobId, QStringView dir, int fileCount ) {
	Q_UNUSED( jobId );
	showBanner( QStringLiteral( "已保存 %1 个推理结果到 %2" ).arg( fileCount ).arg( dir ), BannerSuccess );
}

// 检测器按模型对齐了配置尺寸：同步到 App 的配置副本。
// 不做这一步，侧边栏会一直显示用户填的旧尺寸，而检测器实际用的是模型尺寸。
void DetectionApp::onConfigAdjusted( const DetectorConfig& config ) {
	this->m_config = config;
	emit configChanged();
	Logger::instance()->info( QStringLiteral( "[DetectionApp]: 配置已按模型对齐 -> " ) + configSummary() );
}

void DetectionApp::setState( quint64 currentJobId, bool progressInterminate, bool busy ) {
	m_currentJobId = currentJobId;
	m_progressIndeterminate = progressInterminate;
	setBusy( busy );
	emit progressChanged();
}