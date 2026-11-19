#include "CudaDevice.h"
#include <QScopeGuard>
extern "C" {
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext_cuda.h>
}
namespace {
	// 将ffmpeg错误转为字符串
	QString ffmpegErrorToString( int errnum ) {
		char buf[AV_ERROR_MAX_STRING_SIZE] = { 0 };
		av_strerror( errnum, buf, sizeof( buf ) );
		return QString::fromLocal8Bit( buf );
	}
	// 将CUDA错误转为字符串
	QString cudaErrorToString( cudaError_t err ) {
		return QString::fromLocal8Bit( cudaGetErrorString( err ) );
	}
	// 创建 DetectionError(创建失败自动触发fallback，所以错误码默认为fallbackRequested)
	DetectionError makeError( const QString& msg,DetectionErrorCode code = DetectionErrorCode::FallbackRequested ) {
		DetectionError error;
		error.errorMessage = msg;
		error.errorCategory = DetectionErrorCategory::CUDA;
		error.errorCode = code;
		return error;
	}
}

// 初始化设备（幂等：已初始化直接返回true）
bool CudaDevice::init( int deviceId ) {
	if (isValid()) {
		if (m_deviceId == deviceId)
			return true;
		m_lastError = makeError(QStringLiteral("[CudaDevice]: 活跃会话不支持切换CUDA设备"),
			DetectionErrorCode::InvalidConfig);
		return false;
	}
	m_lastError = {};
	int deviceCount = 0;
	auto cudaErr = cudaGetDeviceCount(&deviceCount);
	if (cudaErr != cudaSuccess) {
		m_lastError = makeError(QStringLiteral("[CudaDevice]: 查询CUDA设备失败: ") + cudaErrorToString(cudaErr));
		return false;
	}
	if (deviceId < 0 || deviceId >= deviceCount) {
		m_lastError = makeError(QStringLiteral("[CudaDevice]: 设备ID无效: %1").arg(deviceId),
			DetectionErrorCode::InvalidConfig);
		return false;
	}
	cudaErr = cudaSetDevice(deviceId);
	if (cudaErr != cudaSuccess) {
		m_lastError = makeError(QStringLiteral("[CudaDevice]: 设置CUDA设备失败: ") + cudaErrorToString(cudaErr));
		return false;
	}

	cudaStream_t stream = nullptr;
	AVBufferRef* hwDevice = nullptr;
	const auto cleanup = qScopeGuard([&] {
		av_buffer_unref(&hwDevice);
		if (stream)
			cudaStreamDestroy(stream);
	});
	cudaErr = cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking);
	if (cudaErr != cudaSuccess) {
		m_lastError = makeError(QStringLiteral("[CudaDevice]: 创建CUDA流失败: ") + cudaErrorToString(cudaErr));
		return false;
	}

	const auto deviceName = QByteArray::number(deviceId);
	const int ffmpegErr = av_hwdevice_ctx_create(&hwDevice, AV_HWDEVICE_TYPE_CUDA,
		deviceName.constData(), nullptr, AV_CUDA_USE_CURRENT_CONTEXT);		// 更正为使用当前的CUDA流
	if (ffmpegErr < 0) {
		m_lastError = makeError(QStringLiteral("[CudaDevice]: 创建FFmpeg CUDA hwdevice失败: ") + ffmpegErrorToString(ffmpegErr));
		return false;
	}
	// 保证当前的ffmpeg的硬件上下文能够与当前的CUDA流进行生命周期绑定
	auto* hw = reinterpret_cast<AVHWDeviceContext*>(hwDevice->data);
	auto* cuda = reinterpret_cast<AVCUDADeviceContext*>(hw->hwctx);
	if (!cuda || !cuda->cuda_ctx) {
		m_lastError = makeError(QStringLiteral("[CudaDevice]: FFmpeg未取得当前CUDA上下文"));
		return false;
	}
	cuda->stream = reinterpret_cast<CUstream>(stream);
	// 获取显存信息填充设备名+显存
	cudaDeviceProp prop{};
	cudaErr = cudaGetDeviceProperties(&prop, deviceId);
	if (cudaErr != cudaSuccess) {
		m_lastError = makeError(QStringLiteral("[CudaDevice]: 查询CUDA设备属性失败: ") + cudaErrorToString(cudaErr));
		return false;
	}
	size_t freeMem = 0, totalMem = 0;
	cudaErr = cudaMemGetInfo(&freeMem, &totalMem);
	if (cudaErr != cudaSuccess) {
		m_lastError = makeError(QStringLiteral("[CudaDevice]: 查询CUDA显存失败: ") + cudaErrorToString(cudaErr));
		return false;
	}
	m_deviceName = QStringLiteral("%1 (显存: %2 MB)").arg(QString::fromUtf8(prop.name)).arg(totalMem / (1024 * 1024));
	m_hwDevice = hwDevice;
	m_stream = stream;
	m_deviceId = deviceId;
	hwDevice = nullptr;
	stream = nullptr;
	return true;
}

bool CudaDevice::isValid() const {
	return m_deviceId != -1  && m_stream && m_hwDevice;
}

QString CudaDevice::describe() const {
	if (m_deviceId == -1 || !isValid())
		return "[CudaDevice]: 未初始化";
	return QString( "[CudaDevice]: 设备ID: %1, 名称: %2" ).arg( m_deviceId ).arg( m_deviceName.isEmpty() ? "未知" : m_deviceName );
}

QString CudaDevice::deviceName() const {
	return m_deviceName.isEmpty() ? "未知" : m_deviceName;
}

// 获取最后一次错误
DetectionError CudaDevice::lastError() const {
	return m_lastError;
}

// 获取统一的CUDA流
cudaStream_t CudaDevice::stream() const {
	return m_stream;
}

// 借出FFmpeg hwdevice的引用
AVBufferRef* CudaDevice::borrowHwDevice() {
	if (!isValid())
		return nullptr;
	// 借出引用，调用方负责 unref
	return av_buffer_ref( m_hwDevice );
}
// 使用
void CudaDevice::use() const {
	if (isValid()) {
		cudaSetDevice( m_deviceId );
	}
}

CudaDevice::~CudaDevice() {
	if (m_deviceId >= 0)
		cudaSetDevice(m_deviceId);
	// 等待流完成
	if (m_stream)
		cudaStreamSynchronize(m_stream);
	// 析构时释放引用和资源
	if (m_hwDevice) {
		av_buffer_unref( &m_hwDevice );
		m_hwDevice = nullptr;
	}
	if (m_stream) {
		cudaStreamDestroy( m_stream );
		m_stream = {};
	}
}

// 获取单例
CudaDevice& CudaDevice::instance() {
	static CudaDevice instance;
	return instance;
}


// 获取设备ID
int CudaDevice::deviceId() const {
	return m_deviceId;
}