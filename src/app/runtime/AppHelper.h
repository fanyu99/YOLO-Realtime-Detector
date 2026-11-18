#pragma once
#include <QObject>
#include <QProcess>
#include <QCoreApplication>
// 重启应用
class AppHelper : public QObject
{
	Q_OBJECT
public:
	explicit AppHelper ( QObject* parent = nullptr ) : QObject ( parent ) {}
	~AppHelper () = default;
	// 重启应用
	Q_INVOKABLE void restart () {
		QString appPath = QCoreApplication::applicationFilePath ();
		QProcess::startDetached ( appPath, QCoreApplication::arguments () ); // 重新启动一个进程
		QCoreApplication::quit ();
	}
};
