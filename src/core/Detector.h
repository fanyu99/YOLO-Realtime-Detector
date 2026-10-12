#pragma once
#include <onnxruntime_cxx_api.h>
#include <opencv2/opencv.hpp>
#include <QVector>
#include <QString>
#include <atomic>
#include <vector>
#include <span>
#include "Box.h"
#include "DetectionTypes.h"

// 检测器：负责 ONNX 模型的加载与推理
// 错误通过 lastError() 获取
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
	// 最近一次错误(调用方主动查询)
	const DetectorError& lastError() const { return m_lastError; }
	// 构造/配置期产生的警告(非致命)：如输入尺寸已按模型自动对齐。
	const QVector<DetectorWarning>& warnings() const { return m_warnings; }
	// 更改配置：返回是否成功(仅更新配置与输入尺寸，不重建会话)
	bool setConfig(const DetectorConfig& config_);
	// 预热：用假输入跑若干次 Run
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
	// num_anchors 由输出张量反推(output_shape[2])
	// 内部使用 std::vector 直接喂给 OpenCV NMS，避免 QVector<->std::vector 适配拷贝
	std::vector<Box> parse_yolo_ouput(float* data, int num_classes, int num_anchors)const;
	// 将预测框从 letterbox 后的 640x640 空间映射回原图(返回新容器)
	QVector<Box> map_boxes_to_original_image(std::span<const Box> boxes, const LetterboxInfo& info) const;
	// 就地映射：直接改写调用方容器，省掉一次结果容器的构造与拷贝。
	// 接受任何连续内存容器(QVector/std::vector/数组)，非连续容器请先转为连续容器。
	void map_boxes_to_original_image_replace(std::span<Box> boxes, const LetterboxInfo& info);
private:
	// 创建会话选项
	static Ort::SessionOptions CreateSessionOptions(int threads, bool use_cuda, int device_id, bool& cuda_ok);
	// 按模型真实输入 shape 校正 config 与 input_shape；不符时生成警告。
	// 模型为动态维(-1)时以配置尺寸为准。构造期与 setConfig() 共用。
	void alignInputSizeToModel();
	// 真正的 ONNX 推理段：内部 try/catch，任何异常都转成错误结果返回，
	DetectionResult runInference(cv::Mat& chw, const LetterboxInfo& info);
	DetectorConfig config;	// 配置
	QString input_name;					 // 模型输入名
	QString output_name;									  // 模型输出名
	QVector<int64_t> input_shape;			   // 模型要求的输入shape
	std::vector<int64_t> m_model_input_shape;	// 模型真实输入 shape(构造期读取)
	QVector<DetectorWarning> m_warnings;		// 构造/配置期警告
	Ort::Env env;
	Ort::Session session;		// 对话,不能拷贝
	Ort::MemoryInfo mem_info;	 // 预分配内存信息
	bool cuda_enabled=false;	// CUDA 是否真正启用成功
	bool m_valid = false;		// 会话是否构造成功
	DetectorError m_lastError;	// 最近一次错误
};
