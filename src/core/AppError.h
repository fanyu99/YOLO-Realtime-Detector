#pragma once
#include <QObject>
#include <QString>
#include <QtQmlIntegration/QtQmlIntegration>

// 应用层错误
struct AppError {
	Q_GADGET
	QML_ELEMENT		 // 用于QML侧直接使用相关属性
public:
	// 错误码
	enum class Code {
		DetectorError,		// 检测器错误
		InvalidModelPath,	// 模型路径无效
		InvalidFilePath,	// 文件路径无效
		InvalidConfig,		// 配置无效
		DetectionFailed,	// 检测失败
		UnknownError
	};
	Q_ENUM(Code)

	Q_PROPERTY(QString errorMessage READ errorMessageText)
	Q_PROPERTY(Code errorCode READ errorCodeValue)

	QString errorMessage;	// 错误信息
	Code errorCode;			// 错误代码

	AppError(QStringView msg, Code code = Code::UnknownError) : errorMessage(msg.toString()), errorCode(code) {}

	// getter 供 QML 读取
	QString errorMessageText() const { return errorMessage; }
	Code errorCodeValue() const { return errorCode; }
};
using AppErrorCode = AppError::Code;
