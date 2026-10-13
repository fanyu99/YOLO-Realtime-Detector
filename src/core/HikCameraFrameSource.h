#include "FrameSource.h"
#include <QVector>
#include <QString>
// 海康相机SDK帧源,支持设置触发模式

class HikCameraFrameSource: public FrameSource{
public:
    enum class TriggerMode {
		Off,      // 关闭触发,连续采集
        Software, // 软件触发
		Line0,    // 硬触发,使用Line0
		Line1,    // 硬触发,使用Line1
		Line2,    // 硬触发,使用Line2
    };
    struct DeviceInfo {
        QString serial;
        QString model;
        QString userDefinedName;
        bool online = false;
    };
	bool isOpened() const override;
    static QVector<DeviceInfo>enumerateDevices(); // 枚举在线设备
	static FrameSourcePtr forDevices(QStringView serial, TriggerMode mode = TriggerMode::Off); // 根据序列号创建帧源
    bool open() override;  
    cv::Mat getFrame()override;  // 获取帧,获取到空cv::Mat代表终止,同现在的摄像头源
    cv::Size frameSize()const override;
    QString displayName()const override; //获取相机名称
	QString lastError()const override;   // 最近一次错误(worker 取它上报给上层)
    FrameSourceType type() const override {
        return FrameSourceType::Camera;
    }
    bool setTriggerSoftware(); // 设置软触发模式(需要先暂停采集,后续需要自行重启采集)
	bool setTriggerHardware(int lineId =0 ); // 设置硬触发模式(需要先暂停采集,后续需要自行重启采集)
	bool setTriggerOff(); // 设置触发关闭(需要先暂停采集,设置为连续采集,后续需要自行重启采集)
    // 自适应调整默认的模式
	bool setTriggerAuto(); // 设置触发模式为自动(需要先暂停采集,后续需要自行重启采集)
    void close() override;

    ~HikCameraFrameSource();
private:
	
	struct Camera; // 有关MVS相关的结构体,PIMPL设计避免头文件暴露和编译依赖
	using CameraPtr = std::unique_ptr<Camera>;
	explicit HikCameraFrameSource(const QString& serial);
    CameraPtr m_camera;
    QString m_serial;  // 序列号
	QString m_lastError; // 最近的错误信息
	QString m_displayName; // 显示名称
    TriggerMode m_mode; // 设置的触发模式

};