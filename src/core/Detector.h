#pragma once
#include <onnxruntime_cxx_api.h>
#include <opencv2/opencv.hpp>
#include <QVector>
#include <QString>
#include <atomic>
#include <vector>
#include "Box.h"
#include "DetectionTypes.h"

// 检测器：负责 ONNX 模型的加载与推理
// 不依赖 QObject：错误通过 lastError() 上抛，结果只走返回值通道，
// 因此可以在任意线程使用，不需要 moveToThread。

class Detector {
public:
	Detector();
	Detector(const DetectorConfig& config);
	~Detector();
	Detector& operator=(const Detector&) = delete;
	Detector& operator=(Detector&&) = delete;
	Detector(const Detector&) = delete;
	Detector(Detector&&) = delete;

	// 会话是否构造成功(构造失败时用 lastError() 取原因)
	bool isValid() const { return m_valid; }
	// 最近一次错误(单通道：失败不再发信号，由调用方主动查询)
	const DetectorError& lastError() const { return m_lastError; }
	// 构造/配置期产生的警告(非致命)：如输入尺寸已按模型自动对齐。
	// 与 lastError 分离：警告不意味着失败，调用方应把它显式提示给用户。
	const QVector<DetectorWarning>& warnings() const { return m_warnings; }
	// 更改配置：返回是否成功(仅更新配置与输入尺寸，不重建会话)
	bool setConfig(const DetectorConfig& config_);
	// 预热：用假输入跑若干次 Run，把 ORT / CUDA EP 的延迟初始化开销
	// (CUDA context、cuDNN handle、内存 arena 首次分配等，可达数百 ms)
	// 提前烧掉，避免用户的第一帧检测付出这笔一次性成本。
	// 失败只记日志、返回 false，不影响后续正常检测。
	bool warmup(int iterations = 3);
	// 检测目标
	DetectionResult detect(const QString& image_path);	// 传路径
	DetectionResult detect(const cv::Mat& image);		// 传图/帧
	// 传视频路径 / 视频流；stop 非空时逐帧检查，用于取消长作业(如摄像头流)
	QVector<DetectionResult> detect_video(const QString& video_path, std::atomic<bool>* stop = nullptr);
	QVector<DetectionResult> detect_video(cv::VideoCapture& cap, std::atomic<bool>* stop = nullptr);
	// 当前推理实际使用的设备："CUDA" 或 "CPU"
	const char* device() const;
	bool usingCUDA() const;
	// letterbox预处理	(后续需要RGB和归一化以及重排布局为NCHW)
	// 失败返回空 Mat，原因见 lastError()
	cv::Mat letterbox(const cv::Mat& src, LetterboxInfo& info, int dst_w=640,int dst_h=640,cv::Scalar color = cv::Scalar(114, 114, 114),int interp = cv::INTER_LINEAR );
	// 颜色转换并归一化并转换为NCHW布局，失败返回 false
	bool preprocess(const cv::Mat& src, cv::Mat&chw, int dst_w=640, int dst_h=640);
	// 解析推理结果
	// num_anchors 必须由输出张量反推(output_shape[2])，不能写死 8400 ——
	// 8400 只在 640x640 输入下成立，输入尺寸一变它就是错的(320->2100, 1280->33600)
	QVector<Box> parse_yolo_ouput(float* data, int num_classes, int num_anchors)const;
	// 将预测框从 letterbox 后的 640x640 空间映射回原图
	QVector<Box> map_boxes_to_original_image(const QVector<Box>& boxes, const LetterboxInfo& info) const;
private:
	// 创建会话选项
	static Ort::SessionOptions CreateSessionOptions(int threads, bool use_cuda, int device_id, bool& cuda_ok);
	// 按模型真实输入 shape 校正 config 与 input_shape；不符时生成警告。
	// 模型为动态维(-1)时以配置尺寸为准。构造期与 setConfig() 共用。
	void alignInputSizeToModel();
	// 真正的 ONNX 推理段：内部 try/catch，任何异常都转成错误结果返回，
	// 绝不让异常逃出 Detector(否则会穿到线程入口触发 std::terminate)
	DetectionResult runInference(cv::Mat& chw, const LetterboxInfo& info);
	DetectorConfig config;
	QString input_name;
	QString output_name;
	QVector<int64_t> input_shape;
	std::vector<int64_t> m_model_input_shape;	// 模型真实输入 shape(构造期读取)
	QVector<DetectorWarning> m_warnings;		// 构造/配置期警告
	Ort::Env env;
	Ort::Session session;
	Ort::MemoryInfo mem_info;
	bool cuda_enabled=false;	// CUDA 是否真正启用成功
	bool m_valid = false;		// 会话是否构造成功
	DetectorError m_lastError;	// 最近一次错误
};
