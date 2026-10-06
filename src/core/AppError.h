#pragma once
#include <QObject>
#include <QString>
#include <QtQmlIntegration/QtQmlIntegration>

// 应用层错误
// Q_GADGET + QML_ELEMENT 使该结构体可以在 QML 中作为值类型使用，
// 这样 QML 侧的 errorOccurred(error) 才能读取 error.errorMessage / error.errorCode
struct AppError {
	Q_GADGET
	QML_ELEMENT
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

	AppError(const QString& msg, Code code = Code::UnknownError) : errorMessage(msg), errorCode(code) {}

	// 以下 getter 供 QML 读取
	QString errorMessageText() const { return errorMessage; }
	Code errorCodeValue() const { return errorCode; }
};

// 兼容原有命名：枚举已移入 AppError 内部，这里用别名保持原有写法可用
using AppErrorCode = AppError::Code;
