#include <MvCameraControl.h>
#include "HikCameraFrameSource.h"
#include <opencv2/opencv.hpp>
#include <QStringList>
#include "Logger.h"
namespace {
	// 获取受支持的枚举值
	bool getSupportedEnumValues(void* handle, const char* featureName, MVCC_ENUMVALUE& values) {
		const int ret = MV_CC_GetEnumValue(handle, featureName, &values);
		if (ret != MV_OK) {
			Logger::instance()->error(QStringLiteral("获取%1支持值失败: %2")
				.arg(QString::fromLatin1(featureName)).arg(ret));
			return false;
		}

		QStringList supportedValues;
		for (unsigned int i = 0; i < values.nSupportedNum; ++i) {
			MVCC_ENUMENTRY entry{};
			entry.nValue = values.nSupportValue[i];
			if (MV_CC_GetEnumEntrySymbolic(handle, featureName, &entry) == MV_OK) {
				supportedValues.append(QStringLiteral("%1 (%2)")
					.arg(QString::fromLatin1(entry.chSymbolic)).arg(entry.nValue));
			}
			else {
				supportedValues.append(QString::number(values.nSupportValue[i]));
			}
		}
		Logger::instance()->info(QStringLiteral("设备支持的%1: %2")
			.arg(QString::fromLatin1(featureName), supportedValues.join(QStringLiteral(", "))));
		return true;
	}
	// 是否为受支持的枚举值
	bool supportsEnumValue(const MVCC_ENUMVALUE& values, unsigned int value) {
		for (unsigned int i = 0; i < values.nSupportedNum; ++i) {
			if (values.nSupportValue[i] == value)
				return true;
		}
		return false;
	}
	// 获取设备支持的采集模式、触发模式、触发源并显示
	bool logSupportedCaptureModes(void* handle, MVCC_ENUMVALUE& acquisitionModes,
		MVCC_ENUMVALUE& triggerModes, MVCC_ENUMVALUE& triggerSources) {
		if (!getSupportedEnumValues(handle, "AcquisitionMode", acquisitionModes)
			|| !getSupportedEnumValues(handle, "TriggerMode", triggerModes)) {
			return false;
		}
		if (supportsEnumValue(triggerModes, MV_TRIGGER_MODE_ON))
			return getSupportedEnumValues(handle, "TriggerSource", triggerSources);

		Logger::instance()->info(QStringLiteral("设备不支持开启触发模式"));
		return true;
	}

	// MV_CC_Initialize / MV_CC_Finalize 是进程级的，只做一次
	bool ensureSdkInitialized() {
		static const bool ok = (MV_CC_Initialize() == MV_OK);
		if (!ok)
			Logger::instance()->error(QStringLiteral("初始化MVS SDK失败"));
		return ok;
	}

	// 停流 -> 关设备 -> 销毁句柄
	// 删除器
	struct Handle_Deleter {
		void operator()(void* handle) const {
			if (!handle)
				return;
			MV_CC_StopGrabbing(handle);
			MV_CC_CloseDevice(handle);
			MV_CC_DestroyHandle(handle);
		}
	};
	// 将所有的SDK图像转化为cv::Mat
	inline cv::Mat convertToMat(const unsigned char* pData, const MV_FRAME_OUT_INFO_EX& info) {
		if (!pData) return cv::Mat();
		const int w = static_cast<int>(info.nWidth);
		const int h = static_cast<int>(info.nHeight);
		if (w <= 0 || h <= 0) return cv::Mat();
		unsigned char* data = const_cast<unsigned char*>(pData);
		switch (info.enPixelType)
		{
		case PixelType_Gvsp_Mono8:
			return cv::Mat(h, w, CV_8UC1, data).clone();
		case PixelType_Gvsp_BayerRG8: {
			const cv::Mat raw(h, w, CV_8UC1, data);
			cv::Mat bgr;
			cv::cvtColor(raw, bgr, cv::COLOR_BayerRG2BGR); // cvtColor 自己会分配，无需再 clone
			return bgr;
		}
		case PixelType_Gvsp_BayerGB8: {
			const cv::Mat raw(h, w, CV_8UC1, data);
			cv::Mat bgr;
			cv::cvtColor(raw, bgr, cv::COLOR_BayerGB2BGR);
			return bgr;
		}
		case PixelType_Gvsp_RGB8_Packed: {
			const cv::Mat rgb(h, w, CV_8UC3, data);
			cv::Mat bgr;
			cv::cvtColor(rgb, bgr, cv::COLOR_RGB2BGR);
			return bgr;
		}
		case PixelType_Gvsp_BGR8_Packed:
			return cv::Mat(h, w, CV_8UC3, data).clone();
		case PixelType_Gvsp_YUV422_Packed: {
			const cv::Mat yuyv(h, w, CV_8UC2, data);
			cv::Mat bgr;
			cv::cvtColor(yuyv, bgr, cv::COLOR_YUV2BGR_YUYV);
			return bgr;
		}
		default:
			Logger::instance()->error(QStringLiteral("不支持的像素格式: %1").arg(info.enPixelType));
			return cv::Mat();
		}
	}


};

// PIMPL设计
struct HikCameraFrameSource::Camera {
	using HandlePtr = std::unique_ptr<void, Handle_Deleter>;
	std::unique_ptr<MV_CC_DEVICE_INFO> deviceInfo = nullptr;
	std::unique_ptr<void, Handle_Deleter> handle = nullptr;
};

HikCameraFrameSource::HikCameraFrameSource(const QString& serial) : m_camera(std::make_unique<Camera>()), m_serial(serial) {
}

HikCameraFrameSource::~HikCameraFrameSource() {
	close();
}

// 获取相机的尺寸
cv::Size HikCameraFrameSource::frameSize() const
{
	if (!m_camera || !m_camera->handle)
	{
		return cv::Size();
	}
	// 获取ROI尺寸
	MVCC_INTVALUE stW = { 0 };
	MVCC_INTVALUE stH = { 0 };
	int ret = MV_CC_GetIntValue(m_camera->handle.get(), "Width", &stW);
	if (ret != MV_OK) {
		return cv::Size();
	}
	ret = MV_CC_GetIntValue(m_camera->handle.get(), "Height", &stH);
	if (ret != MV_OK) {
		return cv::Size();
	}
	return cv::Size(stW.nCurValue, stH.nCurValue);
}

// 打开设备并开始采集
bool HikCameraFrameSource::open() {
	// 必须在任何 MV_CC_* 调用之前；句柄是在 forDevices 里建的，
	// 那里也要先初始化，不能等到这里
	if (!ensureSdkInitialized()) {
		m_lastError = QStringLiteral("初始化SDK失败");
		return false;
	}
	// 打开相机
	if (!m_camera || !m_camera->handle) {
		Logger::instance()->error(QStringLiteral("相机未初始化"));
		m_lastError = QStringLiteral("相机未初始化");
		return false;
	}
	// 设置显示名称
	if (m_camera->deviceInfo->nTLayerType == MV_GIGE_DEVICE) {
		const unsigned int ip = m_camera->deviceInfo->SpecialInfo.stGigEInfo.nCurrentIp;
		const QString ipText = QStringLiteral("%1.%2.%3.%4")
			.arg((ip >> 24) & 0xFF).arg((ip >> 16) & 0xFF).arg((ip >> 8) & 0xFF).arg(ip & 0xFF);
		m_displayName = QString::fromLatin1(reinterpret_cast<const char*>(m_camera->deviceInfo->SpecialInfo.stGigEInfo.chModelName))
			+ QStringLiteral(" ") + ipText;
	}
	else if (m_camera->deviceInfo->nTLayerType == MV_USB_DEVICE) {
		m_displayName = QString::fromLatin1(reinterpret_cast<const char*>(m_camera->deviceInfo->SpecialInfo.stUsb3VInfo.chModelName));
	}
	// 打开设备
	int ret = MV_CC_OpenDevice(m_camera->handle.get());
	if (ret != MV_OK) {
		Logger::instance()->error(QStringLiteral("打开相机失败: %1").arg(ret));
		m_lastError = QStringLiteral("打开相机失败: %1").arg(ret);
		return false;
	}
	// 设置像素格式为BGR
	ret = MV_CC_SetEnumValue(m_camera->handle.get(), "PixelFormat", PixelType_Gvsp_BGR8_Packed);
	if (ret != MV_OK) {
		Logger::instance()->error(QStringLiteral("设置BGR像素格式失败: %1").arg(ret));
		m_lastError = QStringLiteral("设置BGR像素格式失败: %1").arg(ret);
		return false;
	}
	// 优先使用用户设置的采集模式/触发模式
	switch (m_mode)
	{
	case TriggerMode::Off:
		if (!setTriggerOff()) {
			Logger::instance()->error(QStringLiteral("设置触发模式失败: %1").arg(m_lastError));
			return false;
		}
		break;
	case TriggerMode::Software:
		if (!setTriggerSoftware()) {
			Logger::instance()->error(QStringLiteral("设置触发模式失败: %1").arg(m_lastError));
			return false;
		}
		break;
	case TriggerMode::Line0:
		if (!setTriggerHardware(0)) {
			Logger::instance()->error(QStringLiteral("设置触发模式失败: %1").arg(m_lastError));
			return false;
		}
		break;
	case TriggerMode::Line1:
		if (!setTriggerHardware(1)) {
			Logger::instance()->error(QStringLiteral("设置触发模式失败: %1").arg(m_lastError));
			return false;
		}
		break;
	case TriggerMode::Line2:
		if (!setTriggerHardware(2)) {
			Logger::instance()->error(QStringLiteral("设置触发模式失败: %1").arg(m_lastError));
			return false;
		}
		break;
	default:
	// 自适应设置模式,默认使用连续采集模式
		if (!setTriggerAuto()) {
			Logger::instance()->error(QStringLiteral("设置触发模式失败: %1").arg(m_lastError));
			return false;
		}
	}
	// 开始采集
	ret = MV_CC_StartGrabbing(m_camera->handle.get());
	if (ret != MV_OK) {
		Logger::instance()->error(QStringLiteral("开始采集失败: %1").arg(ret));
		m_lastError = QStringLiteral("开始采集失败: %1").arg(ret);
		return false;
	}
	return true;
}

// 获取帧
cv::Mat HikCameraFrameSource::getFrame() {
	if (!m_camera || !m_camera->handle) {
		Logger::instance()->error(QStringLiteral("相机未初始化"));
		m_lastError = QStringLiteral("相机未初始化");
		return cv::Mat();
	}

	MV_FRAME_OUT frame{};
	int ret = MV_CC_GetImageBuffer(m_camera->handle.get(), &frame, 1000);
	if (ret != MV_OK) {
		// 重试两次,重试1秒
		bool retryOK = false;
		for (int i = 0;i < 2;i++) {
			ret = MV_CC_GetImageBuffer(m_camera->handle.get(), &frame, 500);
			if (ret == MV_OK) {
				retryOK = true;
				break;
			}
		}
		if (!retryOK) {
			Logger::instance()->error(QStringLiteral("获取帧失败: %1").arg(ret));
			m_lastError = QStringLiteral("获取帧失败: %1").arg(ret);
			return cv::Mat();
		}
	}

	cv::Mat image = convertToMat(frame.pBufAddr, frame.stFrameInfo);
	ret = MV_CC_FreeImageBuffer(m_camera->handle.get(), &frame); // 归还SDK缓存!

	if (ret != MV_OK) {
		Logger::instance()->error(QStringLiteral("归还SDK缓存失败: %1").arg(ret));
		m_lastError = QStringLiteral("归还SDK缓存失败: %1").arg(ret);
	}
	return image;
}

// 枚举设备
QVector<HikCameraFrameSource::DeviceInfo> HikCameraFrameSource::enumerateDevices() {
	if (!ensureSdkInitialized())
		return {};
	QVector<DeviceInfo> devices;
	// 枚举设备
	MV_CC_DEVICE_INFO_LIST deviceList;
	memset(&deviceList, 0, sizeof(MV_CC_DEVICE_INFO_LIST));
	int ret = MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &deviceList);
	if (ret != MV_OK) {
		Logger::instance()->error(QStringLiteral("枚举设备失败: %1").arg(ret));
		return devices;
	}
	for (unsigned int i = 0; i < deviceList.nDeviceNum; ++i) {
		DeviceInfo info;
		MV_CC_DEVICE_INFO* deviceInfo = deviceList.pDeviceInfo[i];
		if (deviceInfo->nTLayerType == MV_GIGE_DEVICE) {
			info.serial = QString::fromLatin1(reinterpret_cast<const char*>(deviceInfo->SpecialInfo.stGigEInfo.chSerialNumber));
			info.model = QString::fromLatin1(reinterpret_cast<const char*>(deviceInfo->SpecialInfo.stGigEInfo.chModelName));
			info.userDefinedName = QString::fromLatin1(reinterpret_cast<const char*>(deviceInfo->SpecialInfo.stGigEInfo.chUserDefinedName));
		}
		else if (deviceInfo->nTLayerType == MV_USB_DEVICE) {
			info.serial = QString::fromLatin1(reinterpret_cast<const char*>(deviceInfo->SpecialInfo.stUsb3VInfo.chSerialNumber));
			info.model = QString::fromLatin1(reinterpret_cast<const char*>(deviceInfo->SpecialInfo.stUsb3VInfo.chModelName));
			info.userDefinedName = QString::fromLatin1(reinterpret_cast<const char*>(deviceInfo->SpecialInfo.stUsb3VInfo.chUserDefinedName));

		}
		info.online = true;
		devices.append(info);
	}
	return devices;
}
// 设备是否打开
bool HikCameraFrameSource::isOpened() const {
	if (!m_camera || !m_camera->handle) {
		return false;
	}
	int ret = MV_CC_IsDeviceConnected(m_camera->handle.get());
	return ret == MV_OK;
}

// 获取相机名称
QString HikCameraFrameSource::displayName()const {
	return m_displayName; // 显示名称
}

// 最近一次错误：worker 在 open() 失败时取它拼成 DetectorError 上报
QString HikCameraFrameSource::lastError()const {
	return m_lastError;
}


// 关闭设备并销毁。
// 收尾全在 Handle_Deleter 里：handle.reset() 会依次停流、关设备、销毁句柄。
void HikCameraFrameSource::close() {
	if (m_camera && m_camera->handle)
		m_camera->handle.reset();
}
//    工厂函数,创建FrameSource
FrameSourcePtr HikCameraFrameSource::forDevices(QStringView serial, TriggerMode mode) {

	if (!ensureSdkInitialized())
		return FrameSourcePtr();

	// 枚举设备获取信息
	MV_CC_DEVICE_INFO_LIST deviceList;
	memset(&deviceList, 0, sizeof(MV_CC_DEVICE_INFO_LIST));
	MV_CC_DEVICE_INFO* deviceInfo = nullptr;
	int ret = MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &deviceList);
	if (ret != MV_OK)
	{
		return FrameSourcePtr();
	}
	// 查找指定序列号的设备
	for (int i = 0;i < deviceList.nDeviceNum;i++)
	{
		if (deviceList.pDeviceInfo[i]->nTLayerType == MV_GIGE_DEVICE)
		{
			QString deviceSerial = QString::fromLatin1(reinterpret_cast<const char*>(deviceList.pDeviceInfo[i]->SpecialInfo.stGigEInfo.chSerialNumber));
			if (deviceSerial == serial)
			{
				deviceInfo = deviceList.pDeviceInfo[i];
				break;
			}
		}
		else if (deviceList.pDeviceInfo[i]->nTLayerType == MV_USB_DEVICE)
		{
			QString deviceSerial = QString::fromLatin1(reinterpret_cast<const char*>(deviceList.pDeviceInfo[i]->SpecialInfo.stUsb3VInfo.chSerialNumber));
			if (deviceSerial == serial)
			{
				deviceInfo = deviceList.pDeviceInfo[i];
				break;
			}
		}
	}
	if (!deviceInfo) {
		return FrameSourcePtr();
	}

	void* handle = nullptr;
	ret = MV_CC_CreateHandle(&handle, deviceInfo);
	if (!handle || ret != MV_OK)
	{
		return FrameSourcePtr();
	}
	HikCameraFrameSource* source = new HikCameraFrameSource(serial);
	source->m_camera->deviceInfo = std::make_unique<MV_CC_DEVICE_INFO>(*deviceInfo); // 深拷贝设备信息防止被释放
	source->m_camera->handle = std::unique_ptr<void, Handle_Deleter>(handle);
	source->m_mode = mode; // 设置触发模式
	return FrameSourcePtr(source);
}



// 设置软触发模式
bool  HikCameraFrameSource::setTriggerSoftware() {
	if (!m_camera || !m_camera->handle) {
		Logger::instance()->error(QStringLiteral("相机未初始化"));
		m_lastError = QStringLiteral("相机未初始化");
		return false;
	}

	// 设置触发模式为软件触发
	int ret = MV_CC_SetEnumValue(m_camera->handle.get(), "TriggerMode", 1); // 1: 开启触发模式
	if (ret != MV_OK) {
		Logger::instance()->error(QStringLiteral("设置触发模式失败: %1").arg(ret));
		m_lastError = QStringLiteral("设置触发模式失败: %1").arg(ret);
		return false;
	}
	ret = MV_CC_SetEnumValue(m_camera->handle.get(), "TriggerSource", MV_TRIGGER_SOURCE_SOFTWARE); // 7: 软件触发
	if (ret != MV_OK) {
		Logger::instance()->error(QStringLiteral("设置触发源失败: %1").arg(ret));
		m_lastError = QStringLiteral("设置触发源失败: %1").arg(ret);
		return false;
	}
	return true;
}

// 设置硬触发模式
bool HikCameraFrameSource::setTriggerHardware(int lineId) {
	if (!m_camera || !m_camera->handle) {
		Logger::instance()->error(QStringLiteral("相机未初始化"));
		m_lastError = QStringLiteral("相机未初始化");
		return false;
	}
	if (lineId < 0 || lineId > 3) {
		Logger::instance()->error(QStringLiteral("设置硬触发模式失败,触发线号无效: %1").arg(lineId));
		m_lastError = QStringLiteral("设置硬触发模式失败,触发线号无效: %1").arg(lineId);
		return false;
	}

	// 设置触发模式为硬触发
	int ret = MV_CC_SetEnumValue(m_camera->handle.get(), "TriggerMode", 1); // 1: 开启触发模式
	if (ret != MV_OK) {
		Logger::instance()->error(QStringLiteral("设置触发模式失败: %1").arg(ret));
		m_lastError = QStringLiteral("设置触发模式失败: %1").arg(ret);
		return false;
	}
	ret = MV_CC_SetEnumValue(m_camera->handle.get(), "TriggerSource", lineId); // 0: 硬件触发
	if (ret != MV_OK) {
		Logger::instance()->error(QStringLiteral("设置触发源失败: %1").arg(ret));
		m_lastError = QStringLiteral("设置触发源失败: %1").arg(ret);
		return false;
	}
	return true;
}

// 关闭触发模式,设置为连续采集模式
bool HikCameraFrameSource::setTriggerOff() {
	if (!m_camera || !m_camera->handle) {
		Logger::instance()->error(QStringLiteral("相机未初始化"));
		m_lastError = QStringLiteral("相机未初始化");
		return false;
	}
	// 先关闭采集
	// 设置触发模式为关闭
	int ret = MV_CC_SetEnumValue(m_camera->handle.get(), "TriggerMode", MV_TRIGGER_MODE_OFF); // 0: 关闭触发模式
	if (ret != MV_OK) {
		Logger::instance()->error(QStringLiteral("设置触发模式失败: %1").arg(ret));
		m_lastError = QStringLiteral("设置触发模式失败: %1").arg(ret);
		return false;
	}
	// 设置连续采集模式
	ret = MV_CC_SetEnumValue(m_camera->handle.get(), "AcquisitionMode", MV_ACQ_MODE_CONTINUOUS); // 0: 连续采集
	if (ret != MV_OK) {
		Logger::instance()->error(QStringLiteral("设置采集模式失败: %1").arg(ret));
		m_lastError = QStringLiteral("设置采集模式失败: %1").arg(ret);
		return false;
	}
	return true;
}


// 设置自适应默认模式
bool HikCameraFrameSource::setTriggerAuto() {
	if (!m_camera || !m_camera->handle) {
		Logger::instance()->error(QStringLiteral("相机未初始化"));
		m_lastError = QStringLiteral("相机未初始化");
		return false;
	}
	MVCC_ENUMVALUE acquisitionModes{};
	MVCC_ENUMVALUE triggerModes{};
	MVCC_ENUMVALUE triggerSources{};
	if (!logSupportedCaptureModes(m_camera->handle.get(), acquisitionModes, triggerModes, triggerSources)) {
		m_lastError = QStringLiteral("读取设备支持的采集/触发模式失败");
		return false;
	}

	// 根据支持的模式设置相机采集模式
	// 默认选择连续采集模式
	int ret = MV_OK;
	if (supportsEnumValue(acquisitionModes, MV_ACQ_MODE_CONTINUOUS)) {
		ret = MV_CC_SetEnumValue(m_camera->handle.get(), "AcquisitionMode", MV_ACQ_MODE_CONTINUOUS);
		if (ret != MV_OK) {
			Logger::instance()->error(QStringLiteral("设置采集模式失败: %1").arg(ret));
			m_lastError = QStringLiteral("设置采集模式失败: %1").arg(ret);
			return false;
		}
	}

	if (supportsEnumValue(triggerModes, MV_TRIGGER_MODE_OFF)) {
		ret = MV_CC_SetEnumValue(m_camera->handle.get(), "TriggerMode", MV_TRIGGER_MODE_OFF);
		if (ret != MV_OK) {
			Logger::instance()->error(QStringLiteral("关闭触发模式失败: %1").arg(ret));
			m_lastError = QStringLiteral("关闭触发模式失败: %1").arg(ret);
			return false;
		}
		Logger::instance()->info(QStringLiteral("自动模式选择: 连续采集并关闭触发"));
		return true;
	}
	// 如果支持触发模式,选择软件触发模式
	if (!supportsEnumValue(triggerModes, MV_TRIGGER_MODE_ON)) {
		Logger::instance()->error(QStringLiteral("设备不支持触发开关配置"));
		m_lastError = QStringLiteral("设备不支持触发开关配置");
		return false;
	}
	if (triggerSources.nSupportedNum == 0) {
		Logger::instance()->error(QStringLiteral("设备没有可用的触发源"));
		m_lastError = QStringLiteral("设备没有可用的触发源");
		return false;
	}

	unsigned int selectedSource = triggerSources.nSupportValue[0];
	if (supportsEnumValue(triggerSources, MV_TRIGGER_SOURCE_SOFTWARE)) {
		selectedSource = MV_TRIGGER_SOURCE_SOFTWARE;
	}
	else {
		const unsigned int lines[] = {
			MV_TRIGGER_SOURCE_LINE0,
			MV_TRIGGER_SOURCE_LINE1,
			MV_TRIGGER_SOURCE_LINE2,
			MV_TRIGGER_SOURCE_LINE3
		};
		for (unsigned int line : lines) {
			if (supportsEnumValue(triggerSources, line)) {
				selectedSource = line;
				break;
			}
		}
	}

	ret = MV_CC_SetEnumValue(m_camera->handle.get(), "TriggerMode", MV_TRIGGER_MODE_ON);
	if (ret != MV_OK) {
		Logger::instance()->error(QStringLiteral("开启触发模式失败: %1").arg(ret));
		m_lastError = QStringLiteral("开启触发模式失败: %1").arg(ret);
		return false;
	}
	ret = MV_CC_SetEnumValue(m_camera->handle.get(), "TriggerSource", selectedSource);
	if (ret != MV_OK) {
		Logger::instance()->error(QStringLiteral("设置触发源失败: %1").arg(ret));
		m_lastError = QStringLiteral("设置触发源失败: %1").arg(ret);
		return false;
	}
	Logger::instance()->info(QStringLiteral("自动模式选择: 触发采集, 触发源=%1").arg(selectedSource));
	return true;
}


