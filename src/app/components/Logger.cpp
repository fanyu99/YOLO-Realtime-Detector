#include "Logger.h"
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QMutexLocker>
#include <QJSEngine>
#include <cstdio>

Logger::Logger( QObject* parent ) : QObject( parent ) {
	s_instance = this;
	// 接管 Qt 自身的日志
	qInstallMessageHandler( &Logger::messageHandler );
}

Logger::~Logger() {
	qInstallMessageHandler( nullptr );
	if (s_instance == this)
		s_instance = nullptr;
}

// 全进程唯一实例：QML 单例与 C++ 侧 instance() 指向同一个对象
Logger* Logger::instance() {
	static Logger instance;		// 函数内静态：首次访问即创建，程序退出时自动析构
	return &instance;
}

// 	创建日志
Logger* Logger::create( QQmlEngine* qmlEngine, QJSEngine* jsEngine ) {
	Q_UNUSED( qmlEngine );
	Q_UNUSED( jsEngine );
	Logger* logger = instance();
	// 实例由 C++ 管理生命周期，避免 QML 引擎回收后又被静态析构重复释放
	QJSEngine::setObjectOwnership( logger, QJSEngine::CppOwnership );
	return logger;
}

void Logger::setLevel( Level level ) {
	m_level = level;
}

Logger::Level Logger::level() const {
	return m_level;
}

void Logger::setConsoleEnabled( bool enabled ) {
	m_consoleEnabled = enabled;
}

void Logger::setFileEnabled( bool enabled ) {
	m_fileEnabled = enabled;
}

void Logger::setFilePath( const QString& path ) {
	m_filePath = path;
}

void Logger::debug( const QString& message ) {
	log( Level::Debug, message );
}

void Logger::info( const QString& message ) {
	log( Level::Info, message );
}

// 间隔输出：计数达到间隔才真正落地一条日志，其余调用直接丢弃
void Logger::infoWithInterval( const QString& message ) {
	if (nextInfoCount())
		log( Level::Info, message );
}

void Logger::success( const QString& message ) {
	log( Level::Success, message );
}

void Logger::warning( const QString& message ) {
	log( Level::Warning, message );
}

void Logger::error( const QString& message ) {
	log( Level::Error, message );
}

void Logger::fatal( const QString& message ) {
	log( Level::Fatal, message );
}

// 记录日志：先做级别过滤，再交给 write 落地
void Logger::log( Level level, const QString& message ) {
	// 级别过滤，如果级别比当前的级别低，忽略
	if (static_cast<int>(level) < static_cast<int>(m_level))
		return;
	write( level, message );
}

// 级别 -> 文本
QString Logger::levelName( Level level ) {
	switch (level) {
	case Level::Debug:   return QStringLiteral( "DEBUG" );
	case Level::Info:    return QStringLiteral( "INFO" );
	case Level::Success: return QStringLiteral( "OK" );
	case Level::Warning: return QStringLiteral( "WARN" );
	case Level::Error:   return QStringLiteral( "ERROR" );
	case Level::Fatal:   return QStringLiteral( "FATAL" );
	}
	return QStringLiteral( "UNKNOWN" );
}

// QtMsgType -> 级别
Logger::Level Logger::levelFromMsgType( QtMsgType type ) {
	switch (type) {
	case QtDebugMsg:    return Level::Debug;
	case QtInfoMsg:     return Level::Info;
	case QtWarningMsg:  return Level::Warning;
	case QtCriticalMsg: return Level::Error;
	case QtFatalMsg:    return Level::Fatal;
	}
	return Level::Info;
}

// 消息处理器：Qt 自身的日志入口
// 注意：这里绝对不能再调用 qDebug/qWarning 输出，否则会无限递归
void Logger::messageHandler( QtMsgType type, const QMessageLogContext& context, const QString& message ) {
	if (!s_instance)
		return;
	// QLoggingCategory 的名字作为来源标签(对应原来 LogManager 里的命名 logger)
	const QString category = context.category ? QString::fromUtf8( context.category ) : QString();
	s_instance->write( levelFromMsgType( type ), message, category );
}

// 落地一条日志
void Logger::write( Level level, const QString& message, const QString& category ) {
	// 兜底过滤：消息处理器路径不经过 log()
	if (static_cast<int>(level) < static_cast<int>(m_level))
		return;

	const QString time = QDateTime::currentDateTime().toString( QStringLiteral( "HH:mm:ss.zzz" ) );
	const QString text = category.isEmpty()
		? message
		: (QStringLiteral( "[" ) + category + QStringLiteral( "] " ) + message);

	// 控制台 / 文件输出需要加锁(可能被工作线程调用)
	{
		QMutexLocker locker( &m_mutex );
		if (m_consoleEnabled) {
			QTextStream out( stdout );
			out << "[" << time << "] [" << levelName( level ) << "] " << text << Qt::endl;
		}
		if (m_fileEnabled) {
			QFile file( m_filePath );
			if (file.open( QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text )) {
				QTextStream out( &file );
				out << "[" << time << "] [" << levelName( level ) << "] " << text << Qt::endl;
			}
		}
	}

	// 在锁外发信号
	emit messageLogged( static_cast<int>(level), levelName( level ), time, text );
}

// 设置日志输出间隔(<=0时设置为不进行间隔)
void Logger::setInfoInterval( int n ) {
	m_infoInterval.store( n <= 0 ? 0 : n, std::memory_order_relaxed );
	// 计数器复位，避免新旧间隔错位
	m_infoCount.store( 0, std::memory_order_relaxed );
}

int Logger::infoInterval() const {
	return m_infoInterval.load( std::memory_order_relaxed );
}

// 计数器+1，并在达到间隔时归零；返回本次是否应该输出
bool Logger::nextInfoCount() {
	const int interval = m_infoInterval.load( std::memory_order_relaxed );
	// <=0 表示不间隔：每次都输出，也不维护计数
	if (interval <= 0)
		return true;
	// CAS 循环
	int current = m_infoCount.load( std::memory_order_relaxed );
	while (true) {
		const int next = (current + 1) % interval;
		if (m_infoCount.compare_exchange_weak( current, next, std::memory_order_relaxed )) // 进行更新计数
			return next == 0;
	}
}

