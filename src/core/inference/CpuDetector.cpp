#include "CpuDetector.h"
#include <QDebug>
#include <QElapsedTimer>
#include <vector>
#include <span>
#include <algorithm>
#include <cmath>
CpuDetector::CpuDetector()
	: env( ORT_LOGGING_LEVEL_WARNING, "CpuDetector" ),
	session{ nullptr },
	mem_info( Ort::MemoryInfo::CreateCpu( OrtArenaAllocator, OrtMemTypeDefault ) )
{
}
CpuDetector::CpuDetector( const DetectorConfig& config_ )
	: config{ config_ },
	env( ORT_LOGGING_LEVEL_WARNING, "CpuDetector" ),
	session{ nullptr },
	mem_info( Ort::MemoryInfo::CreateCpu( OrtArenaAllocator, OrtMemTypeDefault ) ),
	input_shape( { 1, 3, config_.input_height, config_.input_width } )
{
	// 校验路径
	if (!std::filesystem::exists( config_.model_path )) {
		m_lastError = DetectionError(
			QStringLiteral( "[CpuDetector]: 模型路径无效: " ) + QString::fromStdU16String( config_.model_path.u16string() ),
			DetectionErrorCategory::Model,
			DetectionErrorCode::InvalidModelPath );
		return;	// 路径无效就不再尝试建会话
	}

	try {
		// 创建会话选项
		Ort::SessionOptions session_options =
			CreateCpuSessionOptions( config.num_threads );
		 // 会话
		session = Ort::Session( env, config.model_path.c_str(), session_options );

		Ort::AllocatorWithDefaultOptions allocator;
		input_name = QString::fromUtf8( session.GetInputNameAllocated( 0, allocator ).get() );
		output_name = QString::fromUtf8( session.GetOutputNameAllocated( 0, allocator ).get() );

		// 读取模型真实输入 shape并校正配置中的输入尺寸
		m_model_input_shape = session.GetInputTypeInfo( 0 )
			.GetTensorTypeAndShapeInfo().GetShape();
		alignInputSizeToModel();		 // 校正配置的输入尺寸为模型输入尺寸

		m_valid = true;		// 建立成功
	}
	catch (const std::exception& e) {
		m_lastError = DetectionError(
			QStringLiteral( "[CpuDetector]: 初始化检测器失败: " ) + e.what(),
			DetectionErrorCategory::Other,
			DetectionErrorCode::UnknownError );
	}
}
// 更改配置
bool CpuDetector::setConfig( const DetectorConfig& config_ ) {
	if (!config_.isValid()) {
		m_lastError = DetectionError(
			QStringLiteral( "[CpuDetector]: 配置无效: 模型路径: " ) + QString::fromStdString( config_.model_path.string() )
			+ QStringLiteral( " 类别数: " ) + QString::number( config_.class_names.size() )
			+ QStringLiteral( " 置信度阈值: " ) + QString::number( config_.conf_threshold ) + QStringLiteral( " NMS阈值: " ) + QString::number( config_.iou_threshold )
			+ QStringLiteral( " 推理输入(W*H): " )
			+ QString::number( config_.input_width ) + QStringLiteral( "*" ) + QString::number( config_.input_height )
			+ QStringLiteral( " 线程数: " ) + QString::number( config_.num_threads ),
			DetectionErrorCategory::Config,
			DetectionErrorCode::InvalidConfig );
		return false;
	}
	config = config_;
	if (m_valid) {
		// 会话已建立,进行输入尺寸校正
		alignInputSizeToModel();
	}
	else {
		input_shape = { 1, 3, config.input_height, config.input_width };
	}
	return true;
}

// 按模型真实输入 shape 校正 config 与 input_shape。
// 模型输入为静态尺寸且与配置不符时：自动对齐到模型尺寸，并生成一条警告
// (这是"成功但改了参数"，必须让用户显式知晓，所以走 warnings 通道而不是 lastError)。
// 模型输入为动态维(-1)时：以配置尺寸为准。
void CpuDetector::alignInputSizeToModel() {
	m_warnings.clear();

	// 只处理标准的 NCHW 四维输入；其他维度交给 Run() 自己报错
	if (m_model_input_shape.size() != 4) {
		input_shape = { 1, 3, config.input_height, config.input_width };
		return;
	}

	const int64_t model_h = m_model_input_shape[2];
	const int64_t model_w = m_model_input_shape[3];

	// 动态维：模型不指定尺寸，采用配置值
	if (model_h <= 0 || model_w <= 0) {
		m_warnings.push_back( DetectorWarning(
			QStringLiteral( "[CpuDetector]: 模型为动态输入(H/W 含 -1)，采用配置尺寸 " )
			+ QString::number( config.input_width ) + QStringLiteral( "×" ) + QString::number( config.input_height ),
			DetectorWarning::Code::DynamicInputUsed,
			config.input_width, config.input_height,
			config.input_width, config.input_height ) );
		input_shape = { 1, 3, config.input_height, config.input_width };
		return;
	}

	// 静态尺寸且与配置不符：自动对齐 + 警告
	if (model_w != config.input_width || model_h != config.input_height) {
		const int to_w = static_cast<int>(model_w);
		const int to_h = static_cast<int>(model_h);
		m_warnings.push_back( DetectorWarning(
			QStringLiteral( "[CpuDetector]: 模型输入为 " ) + QString::number( to_w ) + QStringLiteral( "×" ) + QString::number( to_h )
			+ QStringLiteral( "，与配置的 " ) + QString::number( config.input_width ) + QStringLiteral( "×" ) + QString::number( config.input_height )
			+ QStringLiteral( " 不一致，已自动按模型尺寸对齐" ),
			DetectorWarning::Code::InputSizeAligned,
			config.input_width, config.input_height,
			to_w, to_h ) );
		config.input_width = to_w;
		config.input_height = to_h;
	}

	input_shape = { 1, 3, config.input_height, config.input_width };
}

// 纯推理：接收预处理结果(NCHW blob)，执行 ORT Run，输出数据拷贝到出参
bool CpuDetector::infer( const CpuPreprocessResult& preprocessed, std::vector<float> &outData, std::vector<int64_t>&outShape ) {
	// 校验输入和配置
	if (!preprocessed.isValid()) {
		m_lastError = DetectionError( QStringLiteral( "[CpuDetector]: 预处理结果无效(blob 为空或 letterbox 信息不全)!" ),
			DetectionErrorCategory::Preprocess, DetectionErrorCode::PreprocessFailed );
		return false;
	}
	if (!config.isValid()) {
		m_lastError = DetectionError( QStringLiteral( "[CpuDetector]: 配置无效!" ),
			DetectionErrorCategory::Config, DetectionErrorCode::InvalidConfig );
		return false;
	}
	if (config.input_width <= 0 || config.input_height <= 0) {
		m_lastError = DetectionError( QStringLiteral( "[CpuDetector]: 输入尺寸非法: " )
			+ QString::number( config.input_width ) + QStringLiteral( "×" ) + QString::number( config.input_height ),
			DetectionErrorCategory::Config, DetectionErrorCode::InvalidConfig );
		return false;
	}
	// 校验是否为NCHW: 1*3*W*H
	const quint64 need = 3ll * config.input_width * config.input_height;
	if (static_cast<quint64>(preprocessed.image.total()) != need) {
		m_lastError = DetectionError( QStringLiteral( "[CpuDetector]: 预处理 blob 尺寸与模型输入不符，期望 " )
			+ QString::number( need ) + QStringLiteral( "，实际 " ) + QString::number( preprocessed.image.total() ),
			DetectionErrorCategory::Preprocess, DetectionErrorCode::PreprocessFailed );
		return false;
	}
	// NCHW blob
	const cv::Mat& chw = preprocessed.image;	// 浅拷贝Mat
	// 进行推理
	try {
		// 0. 校验输入 blob
		if (chw.empty() || !chw.isContinuous() || chw.depth() != CV_32F) {
			m_lastError = DetectionError( QStringLiteral( "[CpuDetector]: 预处理结果无效(NCHW blob 为空或格式不符)" ),
				DetectionErrorCategory::Preprocess, DetectionErrorCode::PreprocessFailed );
			return false;
		}
		// 1. 构造输入tensor
		Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
			mem_info,
			const_cast<float*>(chw.ptr<float>()), // 创建CPUTensor
			chw.total(),
			input_shape.data(),
			static_cast<size_t>(input_shape.size())
		);
		// 2. 推理（使用CPU内存并CPU推理）
		Ort::MemoryInfo cpu_out_mem =
			Ort::MemoryInfo::CreateCpu( OrtDeviceAllocator, OrtMemTypeDefault );
		Ort::IoBinding io_binding{ session };
		io_binding.BindInput( input_name.toUtf8().constData(), input_tensor );
		io_binding.BindOutput( output_name.toUtf8().constData(), cpu_out_mem );

		session.Run( Ort::RunOptions{ nullptr }, io_binding );

		std::vector<Ort::Value> output_tensors = io_binding.GetOutputValues();
		if (output_tensors.empty()) {
			m_lastError = DetectionError( QStringLiteral( "[CpuDetector]: 未返回输出张量" ),
				DetectionErrorCategory::Other, DetectionErrorCode::UnknownError );
			return false;
		}

		// 3. 在 IoBinding 生命周期内拷贝输出数据与 shape 到出参(避免悬空指针)
		auto output_shape = output_tensors[0].GetTensorTypeAndShapeInfo().GetShape();
		const size_t total = output_tensors[0].GetTensorTypeAndShapeInfo().GetElementCount();
		outShape.assign( output_shape.begin(), output_shape.end() );	   // 拷贝到输出shape
		const float* output_data = output_tensors[0].GetTensorData<float>();
		outData.assign( output_data, output_data + total );	// 拷贝到输出的数据
		return true;
	}
	catch (const Ort::Exception& e) {
		m_lastError = DetectionError( QStringLiteral( "[CpuDetector]: ONNX 推理失败: " ) + QString::fromUtf8( e.what() ),
			DetectionErrorCategory::Detection, DetectionErrorCode::DetectionFailed );
		return false;
	}
	catch (const cv::Exception& e) {
		m_lastError = DetectionError( QStringLiteral( "[CpuDetector]: OpenCV 异常: " ) + QString::fromUtf8( e.what() ),
			DetectionErrorCategory::Detection, DetectionErrorCode::DetectionFailed );
		return false;
	}
	catch (const std::exception& e) {
		m_lastError = DetectionError( QStringLiteral( "[CpuDetector]: 推理异常: " ) + QString::fromUtf8( e.what() ),
			DetectionErrorCategory::Other, DetectionErrorCode::UnknownError );
		return false;
	}
	catch (...) {
		m_lastError = DetectionError( QStringLiteral( "[CpuDetector]: 推理时发生未知异常" ),
			DetectionErrorCategory::Other, DetectionErrorCode::UnknownError );
		return false;
	}
}

// 预热
bool CpuDetector::warmup( int iterations ) {
	if (!m_valid || !session || iterations <= 0)
	{
		m_warnings.emplace_back(QStringLiteral("[CpuDetector]: 预热失败(不影响后续检测):未初始化/预热次数不合法"),DetectorWarning::Code::WarmUpFailed);
		return false;
	}

	try {
		// 用与真实推理完全一致的尺寸(已按模型对齐过)构造假输入
		cv::Mat dummy( config.input_height, config.input_width, CV_8UC3, cv::Scalar( 114, 114, 114 ) );
		cv::Mat chw;
		cv::dnn::blobFromImage( dummy, chw, 1.0 / 255.0, cv::Size( config.input_width, config.input_height ),
			cv::Scalar(), true, false, CV_32F );
		if (chw.empty())
			return false;

		for (int i = 0; i < iterations; ++i) {
			Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
				mem_info, chw.ptr<float>(), chw.total(),
				input_shape.data(), static_cast<size_t>( input_shape.size() ) );

			// 与 infer 保持同一条 IoBinding 路径，
			Ort::MemoryInfo cpu_out_mem =
				Ort::MemoryInfo::CreateCpu( OrtDeviceAllocator, OrtMemTypeDefault );
			Ort::IoBinding io_binding{ session };
			io_binding.BindInput( input_name.toUtf8().constData(), input_tensor );
			io_binding.BindOutput( output_name.toUtf8().constData(), cpu_out_mem );

			session.Run( Ort::RunOptions{ nullptr }, io_binding );
			io_binding.GetOutputValues();	// 必须取走输出，否则输出缓冲不参与热机
		}
		return true;
	}
	catch (const std::exception& e) {
		m_warnings.emplace_back( QStringLiteral( "[CpuDetector]: 预热失败(不影响后续检测):" ) + QString::fromUtf8( e.what() ) ,DetectorWarning::Code::WarmUpFailed);
		return false;
	}
	catch (...) {
		m_warnings.emplace_back( QStringLiteral( "[CpuDetector]: 预热失败(不影响后续检测):未知异常" ) ,DetectorWarning::Code::WarmUpFailed);
		return false;
	}
}

// 创建 CPU 会话选项
Ort::SessionOptions CpuDetector::CreateCpuSessionOptions( int threads ) {
	Ort::SessionOptions session_options;
	session_options.SetIntraOpNumThreads( threads );
	return session_options;
}

CpuDetector::~CpuDetector() {
}
