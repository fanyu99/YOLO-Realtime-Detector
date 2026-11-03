#pragma once
#include <QString>
#include <QUrl>
#include <optional>
#include <map>
//  默认的读取和连接超时时间（毫秒）
const int 	DEFAULT_READ_TIMEOUT_MS = 5000;
const int DEFAULT_CONNECT_TIMEOUT_MS = 5000;
// 解码类型：读取状态、错误、流信息、解码配置、解码统计、解码结果等
// 解码读取Packet状态
enum class ReadStatus {
	FrameReady,	  // 有帧可用
	EndOfFile,	  // 正常读取结束
	Paused,	 // 暂停
	Stopped, // 停止
	Error		 // 发生错误
};

// 解码错误
enum class DecodeErrorCode {
	NoError = 0, // 没有错误
	InvalidUrl, // 无效的URL
	InvalidConfig, // 配置无效
	OpenFailed, // 打开失败
	CodecNotFound, // 解码器未找到
	StreamNotFound, // 流未找到
	StreamFailed, // 打开流失败
	DecoderInitFailed, // 解码器初始化失败
	DecodeFailed, // 解码失败
	ReadFailed, // 读取失败
	ReadTimeout, // 读取超时
	Other // 未知错误
};
struct DecodeError {
	QString errorMessage{ QString() };
	DecodeErrorCode errorCode{ DecodeErrorCode::NoError };

	// 工厂函数：
	[[nodiscard]] static DecodeError NoError() {
		return DecodeError{ QString(), DecodeErrorCode::NoError };
	}
	[[nodiscard]] static DecodeError InvalidUrlError( const QString& message = QStringLiteral( "无效的URL" ) ) {
		return DecodeError{ message, DecodeErrorCode::InvalidUrl };
	}
	[[nodiscard]] static DecodeError CodecNotFoundError( const QString& message = QStringLiteral( "解码器未找到" ) ) {
		return DecodeError{ message, DecodeErrorCode::CodecNotFound };
	}
	[[nodiscard]] static DecodeError StreamNotFoundError( const QString& message = QStringLiteral( "流未找到" ) ) {
		return DecodeError{ message, DecodeErrorCode::StreamNotFound };
	}
	[[nodiscard]] static DecodeError StreamFailedError( const QString message = QStringLiteral( "打开流失败" ) ) {
		return DecodeError{ message, DecodeErrorCode::StreamFailed };
	}
	[[nodiscard]] static  DecodeError InvalidConfigError( const QString& message = QStringLiteral( "无效配置" ) ) {
		return DecodeError{ message, DecodeErrorCode::InvalidConfig };
	}
	[[nodiscard]] static DecodeError OtherError( const QString& message = QStringLiteral( "未知错误" ) ) {
		return DecodeError{ message, DecodeErrorCode::Other };
	}
	[[nodiscard]] static DecodeError OpenFailedError( const QString& message = QStringLiteral( "打开失败" ) ) {
		return DecodeError{ message, DecodeErrorCode::OpenFailed };
	}
	[[nodiscard]] static DecodeError InvalidConfigError( const QString& message = QStringLiteral( "无效配置" ) ) {
		return DecodeError{ message, DecodeErrorCode::InvalidConfig };
	}
	[[nodiscard]] static DecodeError CodecNotFoundError( const QString& message = QStringLiteral( "解码器未找到" ) ) {
		return DecodeError{ message, DecodeErrorCode::CodecNotFound };
	}
	[[nodiscard]] static DecodeError InvalidUrlError( const QString& message = QStringLiteral( "无效的URL" ) ) {
		return DecodeError{ message, DecodeErrorCode::InvalidUrl };
	}
	[[nodiscard]] static DecodeError StreamNotFoundError( const QString& message = QStringLiteral( "流未找到" ) ) {
		return DecodeError{ message, DecodeErrorCode::StreamNotFound };
	}
	[[nodiscard]] static DecodeError ReadFailedError( const QString& message = QStringLiteral( "读取失败" ) ) {
		return DecodeError{ message, DecodeErrorCode::ReadFailed };
	}
	[[nodiscard]] static DecodeError ReadTimeoutError( const QString& message = QStringLiteral( "读取超时" ) ) {
		return DecodeError{ message, DecodeErrorCode::ReadTimeout };
	}
	[[nodiscard]] static  DecodeError InvalidConfigError( const QString& message = QStringLiteral( "无效配置" ) ) {
		return DecodeError{ message, DecodeErrorCode::InvalidConfig };
	}
	[[nodiscard]] static DecodeError OtherError( const QString& message = QStringLiteral( "未知错误" ) ) {
		return DecodeError{ message, DecodeErrorCode::Other };
	}
};

// 解码流信息
struct StreamInfo {
	QString containerFormat; // 容器格式
	QString codecName; // 编解码器名称
	QString formatName; // 格式名称
	QString pixelFormat; // 像素格式
	QString colorSpace; // 色彩空间
	QString color_range; // 色彩范围
	AVRational timeBase{ 0.0 }; // 时间基（秒/时间戳单位）
	AVRational frameRate{ 0, 1 }; // 帧率（fps）
	int width{ 0 }; // 视频宽度
	int height{ 0 }; // 视频高度
	int videoStreamIndex{ -1 }; // 视频流索引
	// 是否合法
	[[nodiscard]] bool isValid()const {
		return width > 0 && height > 0
			&& !codecName.isEmpty()
			&& !formatName.isEmpty()
			&& videoStreamIndex >= -1;
	}
	void reset() {
		*this = StreamInfo{};
	}
};

// 解码配置
struct DecodeConfig {
	QUrl url; // URL，本地文件路径、rtsp://
	std::optional<QString> inputFormat; // 输入格式，dshow等
	std::map<QString, QString>options; // 选项，rtsp_transport、stimeout、max_delay、buffer_size
	int connectTimeoutMs{ DEFAULT_CONNECT_TIMEOUT_MS };
	int readTimeoutMs{ DEFAULT_READ_TIMEOUT_MS };
	int videoStreamIndex{ -1 }; // 视频流索引，-1表示自动选择
	// 是否合法
	[[nodiscard]] bool isValid()const {
		return !url.isEmpty() && url.isValid() && connectTimeoutMs > 0 && readTimeoutMs > 0;
	}
};

// 解码统计信息
struct DecodeStatistics {
	quint64 receivedPacketCount{ 0 }; // 收到的packet数（包含非视频包）
	quint64 videoPacketCount{ 0 }; // 视频packet数
	quint64 decodedFrameCount{ 0 }; // 解码帧数
	quint64 displayedFrameCount{ 0 }; // 显示帧数（成功转换并发出frameDecoded）
	quint64 droppedFrameCount{ 0 }; // 覆盖或丢弃帧数（解码后未能显示）
	qint64 lastPts{ -1 }; // 最近一次PTS（-1 = AV_NOPTS_VALUE）
	quint64 lastReadTime{ 0 }; // 最近一次成功读取视频包的时间(us)
	quint64 lastDecodeTime{ 0 }; // 最近一次成功解码的帧的时间
	quint64 decodeErrorCount{ 0 }; // 解码错误数
	void reset() {
		*this = DecodeStatistics{};
	}
};


// 解码结果
struct DecodeResult {
	// 解码后的帧，在Decoder中由getCurrentFrame获取
	// 是借用的AVFrame， 在下一次读取后失效，注意生命周期
	AVFrame* frame{ nullptr };
	QString pixelFormat; // 像素格式
	quint64 readTimeUs{ 0 }; // 接收压缩包的时间
	quint64 decodeTimeUs{ 0 }; // 解码完成的时间
	qint64 pts{ -1 }; // PTS（-1 = AV_NOPTS_VALUE）
	double ptsSeconds{ -1.0 }; // PTS秒数（-1表示无效）
	int frameIndex{ 0 }; // 帧索引
	int width{ 0 }; // 视频宽度
	int height{ 0 }; // 视频高度
};