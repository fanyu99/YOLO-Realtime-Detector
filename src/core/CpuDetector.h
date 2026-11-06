#pragma once
#include <onnxruntime_cxx_api.h>
#include <opencv2/opencv.hpp>
#include <QVector>
#include <QString>
#include <vector>
#include <span>
#include "Box.h"
#include "DetectionTypes.h"
#include "CpuPreprocessor.h"

// CPU 检测器：负责 ONNX 模型的加载与推理（仅 CPU 推理路径）
// 错误通过 lastError() 获取
class CpuDetector {
public:
	CpuDetector();
	// 输入配置，尝试创建对话以及对应的CPU内存及推理器
	CpuDetector( const DetectorConfig& config );
	~CpuDetector();
	CpuDetector& operator=( const CpuDetector& ) = delete;
	CpuDetector& operator=( CpuDetector&& ) = delete;
	CpuDetector( const CpuDetector& ) = delete;
	CpuDetector( CpuDetector&& ) = delete;

	// 会话是否构造成功(构造失败时用 lastError() 取原因)
	bool isValid() const { return m_valid; }
	// 最近一次错误(调用方主动查询)
	const DetectionError& lastError() const { return m_lastError; }
	// 构造/配置期产生的警告(非致命)：如输入尺寸已按模型自动对齐。
	const QVector<DetectorWarning>& warnings() const { return m_warnings; }
	// 更改配置：返回是否成功(仅更新配置与输入尺寸，不重建会话)
	bool setConfig( const DetectorConfig& config_ );
	// 预热：用假输入跑若干次 Run , 失败时填入warnings，不影响后续检测
	bool warmup( int iterations = 3 );
	// 纯推理：接收预处理结果(NCHW blob)，执行 ORT Run
	// 成功返回 true，输出张量数据拷贝到 outData，输出 shape 拷贝到 outShape(供上层反推解析维度)
	// 失败返回 false，原因通过 lastError() 获取
	bool infer( const CpuPreprocessResult& preprocessed, std::vector<float>& outData, std::vector<int64_t>&outShape );
	// 当前推理输入尺寸(已按模型自动对齐)，供预处理使用
	int inputWidth() const { return config.input_width; }
	int inputHeight() const { return config.input_height; }
private:
	// 创建 CPU 会话选项
	static Ort::SessionOptions CreateCpuSessionOptions( int threads );
	// 按模型真实输入 shape 校正 config 与 input_shape；不符时生成警告。
	// 模型为动态维(-1)时以配置尺寸为准。构造期与 setConfig() 共用。
	void alignInputSizeToModel();

	DetectorConfig config;	// 配置
	QString input_name;					 // 模型输入名
	QString output_name;									  // 模型输出名
	QVector<int64_t> input_shape;			   // 模型要求的输入shape
	std::vector<int64_t> m_model_input_shape;	// 模型真实输入 shape(构造期读取)
	QVector<DetectorWarning> m_warnings;		// 构造/配置期警告
	Ort::Env env;
	Ort::Session session;		// 对话,不能拷贝
	Ort::MemoryInfo mem_info;	 // 预分配内存信息
	bool m_valid{ false };		// 会话是否构造成功
	DetectionError m_lastError;	// 最近一次错误
};
