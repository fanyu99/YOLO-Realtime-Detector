#pragma once
#include <QObject>
#include <QString>
#include <QMutex>
#include <atomic>
#include <QtQmlIntegration/QtQmlIntegration>

class QQmlEngine;
class QJSEngine;


// 日志: 支持QML
// 原std日志仓库: 	https://github.com/fanyu99/my_socket/tree/master/logger
//  使用方式：
//    1) QML:  Logger.info("...") / Logger.error("...")
//    2) C++:  Logger::instance()->info("...")，或直接 qInfo() << "..."
class Logger : public QObject
{
	Q_OBJECT
		QML_ELEMENT
		QML_SINGLETON		 // 单例日志
public:
	// 日志级别
	enum class Level {
		Debug = 0,		// 调试
		Info = 1,		// 普通信息
		Success = 2,	// 成功
		Warning = 3,	// 警告
		Error = 4,		// 错误
		Fatal = 5		// 致命
	};
	Q_ENUM( Level )

		~Logger() override;

	static Logger* create( QQmlEngine* qmlEngine, QJSEngine* jsEngine );
	static Logger* instance();

	// 级别阈值：低于该级别的日志会被丢弃
	Q_INVOKABLE void setLevel( Level level );
	Q_INVOKABLE Level level() const;
	// 输出开关
	Q_INVOKABLE void setConsoleEnabled( bool enabled );
	Q_INVOKABLE void setFileEnabled( bool enabled );
	Q_INVOKABLE void setFilePath( const QString& path );

	// 记录日志(QML / 后端)
	Q_INVOKABLE void debug( const QString& message );
	Q_INVOKABLE void info( const QString& message );
	// 间隔输出：每 setInfoInterval 指定的次数才真正输出一次
	// 用于逐帧刷新的推理日志，避免每帧打一条把日志面板刷爆
	Q_INVOKABLE void infoWithInterval( const QString& message );
	Q_INVOKABLE void success( const QString& message );
	Q_INVOKABLE void warning( const QString& message );
	Q_INVOKABLE void error( const QString& message );
	Q_INVOKABLE void fatal( const QString& message );
	Q_INVOKABLE void log( Level level, const QString& message );

	// 设置日志输出间隔(<=0时设置为不进行间隔)
	Q_INVOKABLE void setInfoInterval( int n );
	Q_INVOKABLE int infoInterval() const;

signals:
	// 一条日志：level 为 Level 枚举值, levelName 为级别文本, time 为时间戳, message 为正文
	void messageLogged( int level, const QString& levelName, const QString& time, const QString& message );

private:
	explicit Logger( QObject* parent = nullptr );

	// 消息处理器：把 Qt 自身的 qDebug/qInfo/qWarning/qCritical 统一汇入本日志器
	static void messageHandler( QtMsgType type, const QMessageLogContext& context, const QString& message );
	// 真正落地一条日志(加锁写文件/控制台，锁外发信号)
	void write( Level level, const QString& message, const QString& category = QString() );
	// 级别 -> 文本
	static QString levelName( Level level );
	// QtMsgType -> 级别
	static Level levelFromMsgType( QtMsgType type );

	Level m_level = Level::Info;							// 当前级别阈值
	bool m_consoleEnabled = true;							// 是否输出到控制台
	bool m_fileEnabled = false;								// 是否写入文件
	QString m_filePath = QStringLiteral( "application.log" );	// 日志文件路径
	QMutex m_mutex;											// 保护写文件与信号发射(可能被工作线程调用)
	inline static Logger* s_instance{ nullptr };		// 单例
	//  添加一个info计数,用于视频推理时每隔n个进行一次汇总而不是每帧都输出日志
	std::atomic<int> m_infoCount{ 0 };			// 记录当前的计数
	std::atomic<int> m_infoInterval{ 10 };		// 每隔多少次输出一次(<=0 表示每次都输出)
	// 计数器+1,并在达到间隔时归零；返回本次是否应该输出
	bool nextInfoCount();

};
