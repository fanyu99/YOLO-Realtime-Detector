#include "CudaDevice.h"
extern "C" {
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
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

// 初始化设备
bool CudaDevice::init( int deviceId ) {
	// 幂等
	if (isValid() || m_deviceId != -1) {
		return false;
	}
	// 获取设备数量
	int deviceCount = 0;
	cudaError_t cudaErr = cudaGetDeviceCount( &deviceCount );
	if (deviceId < 0 || deviceId >= deviceCount) {
		m_lastError = makeError( "[CudaDevice]: 设备ID无效: " + QString::number( deviceId ) ,DetectionErrorCode::InvalidConfig);
		return false;
	}

	 // 设置设备
	cudaErr = cudaSetDevice( deviceId );
	if (cudaErr != cudaSuccess) {
		m_lastError = makeError( "[CudaDevice]: 设置CUDA设备失败: " + cudaErrorToString( cudaErr ) );
		return false;
	}
	// 创建CUDA流
	cudaErr = cudaStreamCreateWithFlags( &m_stream, cudaStreamNonBlocking );
	if (cudaErr != cudaSuccess) {
		m_lastError = makeError( "[CudaDevice]: 创建CUDA流失败: " + cudaErrorToString( cudaErr ) );
		m_stream = {};
		return false;
	}
	// 创建FFmpeg hwdevice
	int ffmpegErr = av_hwdevice_ctx_create( &m_hwDevice, AV_HWDEVICE_TYPE_CUDA, nullptr, nullptr, 0 );
	if (ffmpegErr < 0) {
		m_lastError = makeError( "[CudaDevice]: 创建FFmpeg CUDA hwdevice失败: " + ffmpegErrorToString( ffmpegErr ) );
		return false;
	}
	// 获取设备名和显存信息
	cudaDeviceProp prop;
	size_t totalMem = 0;
	cudaGetDeviceProperties( &prop, deviceId );
	cudaMemGetInfo( nullptr, &totalMem );
	m_deviceName = QString( "%1 (显存: %2 MB)" ).arg( prop.name ).arg( totalMem / (1024 * 1024) );

	m_deviceId = deviceId;  // 最后成功了才设置
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