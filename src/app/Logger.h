#pragma once
#include <QObject>
#include <QString>
#include <QMutex>
#include <QtQmlIntegration/QtQmlIntegration>

class QQmlEngine;
class QJSEngine;

// ============================================================================
//  日志器(Qt 风格)
//  由原来的 std 版 logger(D:\C++\project\logger)改写而来，对应关系：
//    std::string             -> QString
//    LogManager 命名 logger  -> QLoggingCategory(类别名作为来源标签)
//    std::cout / std::ofstream -> QTextStream + QFile
//    logDebug/logInfo 自由函数 -> qDebug/qInfo/qWarning/qCritical 宏(经消息处理器统一收集)
//    无 UI 通道              -> messageLogged 信号(QML 日志面板按级别着色显示)
//
//  使用方式：
//    1) QML:  Logger.info("...") / Logger.error("...")
//    2) C++:  Logger::instance()->info("...")，或直接 qInfo() << "..."
// ============================================================================
class Logger : public QObject
{
	Q_OBJECT
	QML_ELEMENT
	QML_SINGLETON
public:
	// 日志级别(数值越大越严重；Success 用于绿色高亮的关键成功节点)
	enum class Level {
		Debug = 0,		// 调试
		Info = 1,		// 普通信息
		Success = 2,	// 成功
		Warning = 3,	// 警告
		Error = 4,		// 错误
		Fatal = 5		// 致命
	};
	Q_ENUM(Level)

	~Logger() override;

	// QML 单例入口：保证 QML 与 C++ 侧拿到的是同一个实例
	static Logger* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);
	// 全局唯一实例(消息处理器内部使用；也可在 main 中提前调用以尽早接管日志)
	static Logger* instance();

	// 级别阈值：低于该级别的日志会被丢弃
	Q_INVOKABLE void setLevel(Level level);
	Q_INVOKABLE Level level() const;
	// 输出开关
	Q_INVOKABLE void setConsoleEnabled(bool enabled);
	Q_INVOKABLE void setFileEnabled(bool enabled);
	Q_INVOKABLE void setFilePath(const QString& path);

	// 记录日志(QML / C++ 均可调用)
	Q_INVOKABLE void debug(const QString& message);
	Q_INVOKABLE void info(const QString& message);
	Q_INVOKABLE void success(const QString& message);
	Q_INVOKABLE void warning(const QString& message);
	Q_INVOKABLE void error(const QString& message);
	Q_INVOKABLE void fatal(const QString& message);
	Q_INVOKABLE void log(Level level, const QString& message);

signals:
	// 一条日志：level 为 Level 枚举值, levelName 为级别文本, time 为时间戳, message 为正文
	void messageLogged(int level, const QString& levelName, const QString& time, const QString& message);

private:
	explicit Logger(QObject* parent = nullptr);

	// 消息处理器：把 Qt 自身的 qDebug/qInfo/qWarning/qCritical 统一汇入本日志器
	static void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message);
	// 真正落地一条日志(加锁写文件/控制台，锁外发信号)
	void write(Level level, const QString& message, const QString& category = QString());
	// 级别 -> 文本
	static QString levelName(Level level);
	// QtMsgType -> 级别
	static Level levelFromMsgType(QtMsgType type);

	Level m_level = Level::Info;							// 当前级别阈值
	bool m_consoleEnabled = true;							// 是否输出到控制台
	bool m_fileEnabled = false;								// 是否写入文件
	QString m_filePath = QStringLiteral("application.log");	// 日志文件路径
	QMutex m_mutex;											// 保护写文件与信号发射(可能被工作线程调用)
	static Logger* s_instance;								// 消息处理器回调需要一个入口
};
