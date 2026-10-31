#pragma once
#include <QString>
#include <QObject>
#include <QUrl>
#include <QImage>
#include <QMetaType>
#include <optional>

// 统一状态：
/*
状态机：
正常流程：Idle -> Opening -> Opened -> Playing -> Stopping -> Stopped -> Idle
重连流程： Playing-> Reconnecting -> Playing
一般错误流程：Playing->Error / Opening -> Error等
中断流程：Playing -> Paused
*/
// 状态的转发流程：
// Worker发送信号 -> Controller接收信号并存储状态 -> QML获取状态
struct RtspOverallStatus {
	Q_GADGET
public:
	enum Status {
		Opening, Opened,
		Error, Idle, Paused,
		Playing, Reconnecting,
		Stopping, Stopped
	};
	Q_ENUM( Status )
};
using RtspStatus = RtspOverallStatus::Status;

// 统一错误类型
struct RtspOverallError {
	Q_GADGET
public:
	// 错误码
	enum ErrorCode {
		NoError = 0, // 没有错误
		InvalidUrl, // 无效的URL
		ConnectionFailed, // 连接失败
		ReconnectFailed, // 重连失败
		AuthenticationFailed, // 身份验证失败
		StreamNotFound, // 流未找到
		StreamFailed, // 打开流失败
		ReadTimeout, // 读取超时
		InvalidConfig, // 配置无效
		AlreadyRunning, // 解码已在运行
		Other // 未知错误
	};
	Q_ENUM( ErrorCode )
public:
	RtspOverallError() = default;
	RtspOverallError( const QString& msg, ErrorCode code ) : errorMessage( msg ), errorCode( code ) {}

	// 供QML进行读取属性
	Q_INVOKABLE QString getErrorMessage() const { return errorMessage; }
	Q_INVOKABLE ErrorCode getErrorCode() const { return errorCode; }

	// 工厂函数
	static RtspOverallError StreamFailedError( const QString message = QStringLiteral( "打开流失败" ) ) {
		return RtspOverallError( message, ErrorCode::StreamFailed );
	}
	static  RtspOverallError InvalidUrlError( const QString& message = QStringLiteral( "无效的URL" ) ) {
		return RtspOverallError{ message, ErrorCode::InvalidUrl };
	}
	static RtspOverallError InvalidConfigError( const QString& message = QStringLiteral( "无效配置" ) ) {
		return RtspOverallError{ message, ErrorCode::InvalidConfig };
	}
	static RtspOverallError ConnectionFailedError( const QString& message = QStringLiteral( "连接失败" ) ) {
		return RtspOverallError{ message, ErrorCode::ConnectionFailed };
	}
	static RtspOverallError AuthenticationFailedError( const QString& message = QStringLiteral( "身份验证失败" ) ) {
		return RtspOverallError{ message, ErrorCode::AuthenticationFailed };
	}
	static RtspOverallError StreamNotFoundError( const QString& message = QStringLiteral( "流未找到" ) ) {
		return RtspOverallError{ message, ErrorCode::StreamNotFound };
	}
	static RtspOverallError ReadTimeoutError( const QString& message = QStringLiteral( "读取超时" ) ) {
		return RtspOverallError{ message, ErrorCode::ReadTimeout };
	}

	QString errorMessage{ QString() };
	ErrorCode errorCode{ ErrorCode::NoError };
};
using RtspError = RtspOverallError;

constexpr int DEFAULT_RTSP_CONNECT_TIMEOUT_MS = 5000; // 默认连接超时时间（毫秒）
constexpr int DEFAULT_RTSP_READ_TIMEOUT_MS = 5000; // 默认读取超时时间（毫秒）
constexpr int DEFAULT_RTSP_MAX_RECONNECT = 5; // 默认最大重连次数

// RTSP协议配置
// 包含用户名、密码、URL、最大重连次数、连接超时时间、读取超时时间和自动重连选项
struct RtspConfig {
	Q_GADGET
public:
	std::optional<QString> username{ std::nullopt }; // 用户名（如果需要身份验证）

	std::optional<QString> password{ std::nullopt }; // 密码（如果需要身份验证）

	QUrl url; // RTSP流的URL

	int maxReconnect{ DEFAULT_RTSP_MAX_RECONNECT }; // 最大重连次数
	int connectTimeoutMs{ DEFAULT_RTSP_CONNECT_TIMEOUT_MS }; // 连接超时时间（毫秒）
	int readTimeoutMs{ DEFAULT_RTSP_READ_TIMEOUT_MS }; // 读取超时时间（毫秒）

	bool autoReconnect{ false }; // 是否自动重连
	// 判断配置是否有效
	bool isValid()const {
		return !url.isEmpty() && url.isValid() && !url.scheme().isEmpty()
			&& maxReconnect >= 0 && connectTimeoutMs > 0 && readTimeoutMs > 0;
	}

	void reset() {
		*this = RtspConfig{};
	}

};

// RTSP协议的数据信息
// 包含容器格式、编解码器名称、格式名称、像素格式、色彩空间、色彩范围、时间基、视频宽度、高度、帧率和视频流索引
struct RtspDataContext {
	Q_GADGET
public:
	QString containerFormat; // 容器格式
	QString codecName; // 编解码器名称
	QString formatName; // 格式名称
	QString pixelFormat; // 像素格式
	QString colorSpace; // 色彩空间
	QString color_range; // 色彩范围
	double timeBase{ 0.0 }; // 时间基（秒/时间戳单位）
	int width{ 0 }; // 视频宽度
	int height{ 0 }; // 视频高度
	int fps{ 0 }; // 帧率
	// 名义帧率：来自流元数据，不代表实际解码速度；实测帧率见 RtspStreamStatus::decodeFps
	int videoStreamIndex{ -1 }; // 视频流索引
	// 是否合法
	bool isValid()const {
		return width > 0 && height > 0 && fps > 0
			&& !codecName.isEmpty() && !formatName.isEmpty() && !pixelFormat.isEmpty()
			&& !colorSpace.isEmpty() && !color_range.isEmpty()
			&& timeBase > 0 && videoStreamIndex >= 0;
	}
	// 重置
	void reset() {
		*this = RtspDataContext{};
	}
};


// 存储的最新视频帧信息
struct LastestFrameStore {
	Q_GADGET
public:
	QImage lastestFrame{}; // 最新的视频帧
	quint64 frameIndex{ 0 }; // 帧索引（用 lastestFrame 判空；原来的 -1 初始化会窄化报错）
	qint64 pts{ -1 }; // pts（-1 = AV_NOPTS_VALUE，不能用 0 表示无效）
	double ptsSeconds{ -1.0 }; // pts秒数（-1 表示无效）
	quint64 readTime{ 0 }; // 接收压缩包的时间
	quint64 decodedTime{ 0 }; // 解码完成的时间
	bool isValid()const {
		// PTS 允许为 -1（AV_NOPTS_VALUE），不能因此把一帧正常画面判为无效
		return !lastestFrame.isNull() && readTime > 0 && decodedTime > 0;
	}
};

// RTSP拉流过程中的统计信息
struct RtspStreamStatus {
	Q_GADGET
	Q_PROPERTY( quint64 receivedPacketCount MEMBER receivedPacketCount )
	Q_PROPERTY( quint64 videoPacketCount MEMBER videoPacketCount )
	Q_PROPERTY( quint64 decodedFrameCount MEMBER decodedFrameCount )
	Q_PROPERTY( quint64 displayedFrameCount MEMBER displayedFrameCount )
	Q_PROPERTY( quint64 droppedFrameCount MEMBER droppedFrameCount )
	Q_PROPERTY( double decodeFps MEMBER decodeFps )
	Q_PROPERTY( double displayFps MEMBER displayFps )
	Q_PROPERTY( qint64 lastPts MEMBER lastPts )
	Q_PROPERTY( quint64 lastReadTime MEMBER lastReadTime )
	Q_PROPERTY( quint64 decodeErrorCount MEMBER decodeErrorCount )
	Q_PROPERTY( int reconnectCount MEMBER reconnectCount )
	Q_PROPERTY( RtspStatus currentStatus MEMBER currentStatus )
public:
	quint64 receivedPacketCount{ 0 }; // 收到的packet数（包含非视频包）
	quint64 videoPacketCount{ 0 }; // 视频packet数
	quint64 decodedFrameCount{ 0 }; // 解码帧数
	quint64 displayedFrameCount{ 0 }; // 显示帧数（成功转换并发出frameDecoded）
	quint64 droppedFrameCount{ 0 }; // 覆盖或丢弃帧数（解码后未能显示）
	double decodeFps{ 0.0 }; // 解码FPS（实测）
	double displayFps{ 0.0 }; // 显示FPS（实测）
	qint64 lastPts{ -1 }; // 最近一次PTS（-1 = AV_NOPTS_VALUE）
	quint64 lastReadTime{ 0 }; // 最近一次成功读取视频包的时间(us)
	quint64 decodeErrorCount{ 0 }; // 解码错误数
	int reconnectCount{ 0 }; // 重连次数（由Controller填写）
	RtspStatus currentStatus{ RtspStatus::Idle }; // 当前状态（由Controller填写）
};

// 跨线程队列连接需要的元类型注册
Q_DECLARE_METATYPE( RtspStatus )
Q_DECLARE_METATYPE( RtspOverallError )
Q_DECLARE_METATYPE( LastestFrameStore )
Q_DECLARE_METATYPE( RtspStreamStatus )
