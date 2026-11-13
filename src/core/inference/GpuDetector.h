#pragma once
#include <onnxruntime_cxx_api.h>
#include <QVector>
#include <QString>
#include <span>
#include "Box.h"
#include "DetectionTypes.h"
#include "CudaPreprocessor.h"


// GPU检测器
class GpuDetector {
public:
	GpuDetector();	// 会话无效，valid=false
	// 构造时会创建CUDA相关环境，如果不支持，设置fallback到Error中，需要手动判断lastError是否存在fallbackRequested
	GpuDetector( const DetectorConfig& config );
	~GpuDetector();
	GpuDetector& operator=( const GpuDetector& ) = delete;
	GpuDetector& operator=( GpuDetector&& ) = delete;
	GpuDetector( const GpuDetector& ) = delete;
	GpuDetector( GpuDetector&& ) = delete;

	// 会话是否成功构造有效
	bool isValid()const;

	// 最近一次错误
	const DetectionError& lastError()const;

	// 生成警告
	const QVector<DetectorWarning>& warnings()const { return m_warnings; };
	// 设置配置
	bool setConfig( const DetectorConfig& config );

	// 预热
	bool warmup( int iterations = 3 );

	// 推理
	bool infer( const GpuPreprocessResult& preprocessed, std::vector<float>& outData, std::vector<int64_t>& outShape );

	// 当前的推理输入尺寸
	inline int inputWidth()const { return m_inputWidth; }
	inline int inputHeight()const { return m_inputHeight; }

private:
	// 创建GPU会话选项(创建CUDA EP到会话中)
	static Ort::SessionOptions CreateSessionOptions( int threads );
	// 按照模型的真实输入尺寸对齐
	void alignInputSizeToModel();

	DetectorConfig m_config;	  // 配置

	QString inputName;	// 模型输入名
	QString outputName;	 // 模型输出名
	std::vector<int64_t>m_inputShape;	// 模型输入尺寸
	std::vector<int64_t>m_model_inputShape;	 // 真实的模型尺寸(构造时读取)
	//	CUDA设备ID:
	int cudaDeviceId = -1;	 // 从CudaDevice获取
	// ORT相关
	Ort::Env env;
	Ort::Session session;
	Ort::MemoryInfo mem_info;
	Ort::MemoryInfo cuda_mem_info; // GPU内存信息

	bool m_valid{ false };	 // 是否有效

	DetectionError m_lastError{};	// 最近一次错误
	QVector<DetectorWarning> m_warnings;	// 构造/配置期产生的警告
	int m_inputWidth = 640;	// 推理输入宽度
	int m_inputHeight = 640;	// 推理输入高度


};