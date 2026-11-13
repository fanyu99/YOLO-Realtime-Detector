
#include "GpuDetector.h"
#ifdef YOLO_HAVE_CUDA
#include "CudaDevice.h"
#endif
bool GpuDetector::setConfig( const DetectorConfig& config_ ) {
	if (!config_.isValid()) {
		m_lastError = DetectionError(
			QStringLiteral( "[GpuDetector]: 配置无效: 模型路径: " ) + QString::fromStdString( config_.model_path.string() )
			+ QStringLiteral( " 类别数: " ) + QString::number( config_.class_names.size() )
			+ QStringLiteral( " 置信度阈值: " ) + QString::number( config_.conf_threshold ) + QStringLiteral( " NMS阈值: " ) + QString::number( config_.iou_threshold )
			+ QStringLiteral( " 推理输入(W*H): " )
			+ QString::number( config_.input_width ) + QStringLiteral( "*" ) + QString::number( config_.input_height )
			+ QStringLiteral( " 线程数: " ) + QString::number( config_.num_threads ),
			DetectionErrorCategory::Config,
			DetectionErrorCode::InvalidConfig );
		return false;
	}
	m_config = config_;
	m_inputHeight = config_.input_height;
	m_inputWidth = config_.input_width;
	if (m_valid) {
		// 会话已建立,进行输入尺寸校正
		alignInputSizeToModel();
	}
	else {
		m_inputShape = { 1, 3, m_config.input_height, m_config.input_width };
	}
	return true;
}

// 对齐输入尺寸到模型真实输入尺寸
void GpuDetector::alignInputSizeToModel() {
	m_warnings.clear();
	// 只处理标准的 NCHW 四维输入；其他维度交给 Run() 自己报错
	if (m_model_inputShape.size() != 4) {
		m_inputShape = { 1, 3, m_config.input_height, m_config.input_width };
		return;
	}
	const int64_t model_h = m_model_inputShape[2];
	const int64_t model_w = m_model_inputShape[3];
	// 动态维：模型不指定尺寸，采用配置值
	if (model_h <= 0 || model_w <= 0) {
		m_warnings.push_back( DetectorWarning(
			QStringLiteral( "[GpuDetector]: 模型为动态输入(H/W 含 -1)，采用配置尺寸 " )
			+ QString::number( m_config.input_width ) + QStringLiteral( "×" ) + QString::number( m_config.input_height ),
			DetectorWarning::Code::DynamicInputUsed,
			m_config.input_width, m_config.input_height,
			m_config.input_width, m_config.input_height ) );
		m_inputShape = { 1, 3, m_config.input_height, m_config.input_width };
		return;
	}
	// 静态尺寸且与配置不符：自动对齐 + 警告
	if (model_w != m_config.input_width || model_h != m_config.input_height) {
		const int to_w = static_cast<int>(model_w);
		const int to_h = static_cast<int>(model_h);
		m_warnings.push_back( DetectorWarning(
			QStringLiteral( "[GpuDetector]: 模型输入为 " ) + QString::number( to_w ) + QStringLiteral( "×" ) + QString::number( to_h )
			+ QStringLiteral( "，与配置的 " ) + QString::number( m_config.input_width ) + QStringLiteral( "×" ) + QString::number( m_config.input_height )
			+ QStringLiteral( " 不一致，已自动按模型尺寸对齐" ),
			DetectorWarning::Code::InputSizeAligned,
			m_config.input_width, m_config.input_height,
			to_w, to_h ) );
		m_config.input_width = to_w;
		m_config.input_height = to_h;
	}
	m_inputShape = { 1, 3, m_config.input_height, m_config.input_width };
	m_inputHeight = m_config.input_height;
	m_inputWidth = m_config.input_width;
}

// 构造时如果不支持CUDA，设置fallback，需要手动判断lastError是否存在fallbackRequested
GpuDetector::GpuDetector()
	:env( ORT_LOGGING_LEVEL_WARNING, "GpuDetector" ),
	session{nullptr},
	mem_info( Ort::MemoryInfo::CreateCpu( OrtDeviceAllocator, OrtMemTypeDefault ) ),
	cuda_mem_info{ nullptr }
{
#ifdef YOLO_HAVE_CUDA
	if (!CudaDevice::instance().isValid()) {
		m_lastError = DetectionError(
			QStringLiteral( "[GpuDetector]: CUDA设备不可用，无法使用GPU检测器" ),
			DetectionErrorCategory::CUDA,
			DetectionErrorCode::FallbackRequested );
		return;
	}
	CudaDevice::instance().use(); // 使用CUDA设备
	cudaDeviceId = CudaDevice::instance().deviceId();
	cuda_mem_info = Ort::MemoryInfo( "Cuda", OrtAllocatorType::OrtArenaAllocator, cudaDeviceId, OrtMemTypeDefault );
#else 
	// 如果没有开启支持则进行fallback
	m_lastError = DetectionError(
		QStringLiteral( "[GpuDetector]: 未开启CUDA支持，无法使用GPU检测器" ),
		DetectionErrorCategory::CUDA,
		DetectionErrorCode::FallbackRequested );
#endif
}

GpuDetector::GpuDetector( const DetectorConfig& config )
	:GpuDetector()
{
#ifdef YOLO_HAVE_CUDA
	// 校验配置是否合法
	if (!CudaDevice::instance().isValid()) {
		m_lastError = DetectionError(
			QStringLiteral( "[GpuDetector]: CUDA设备不可用，无法使用GPU检测器" ),
			DetectionErrorCategory::CUDA,
			DetectionErrorCode::FallbackRequested );
		return;
	}
	if (!std::filesystem::exists( config.model_path )) {
		m_lastError = DetectionError(
			QStringLiteral( "[GpuDetector]: 模型路径无效: " ) + QString::fromStdU16String( config.model_path.u16string() ),
			DetectionErrorCategory::Model,
			DetectionErrorCode::InvalidModelPath );
		m_valid = false;
		return;	
	}
	if (!setConfig( config )) {
		return;
	}	
	// 创建ORT会话
	try {
		// 创建会话选项
		Ort::SessionOptions session_options = CreateSessionOptions( config.num_threads );
		// 创建会话
		session = Ort::Session( env, config.model_path.c_str(), session_options );
		Ort::AllocatorWithDefaultOptions allocator;
		inputName = QString::fromUtf8( session.GetInputNameAllocated( 0, allocator ).get() );
		outputName = QString::fromUtf8( session.GetOutputNameAllocated( 0, allocator ).get() );
		m_model_inputShape = session.GetInputTypeInfo( 0 )
			.GetTensorTypeAndShapeInfo().GetShape();
		alignInputSizeToModel();		 // 校正配置的输入尺寸为模型输入尺寸
		m_valid = true;	// 建立成功
	}
	catch (Ort::Exception& e) {
		m_lastError = DetectionError( QStringLiteral( "[GpuDetector]: 构造时创建会话异常: " ) + QString::fromStdString( e.what() ),
			DetectionErrorCategory::Other, DetectionErrorCode::UnknownError );
		m_valid = false;
	}
	catch (std::exception& e) {
		m_lastError = DetectionError( QStringLiteral( "[GpuDetector]: 构造出现异常: " ) + QString::fromStdString( e.what() ),
			DetectionErrorCategory::Other, DetectionErrorCode::UnknownError );
		m_valid = false;
	}
	catch (...) {
		m_lastError = DetectionError( QStringLiteral( "[GpuDetector]: 构造时发生未知异常" ),
			DetectionErrorCategory::Other, DetectionErrorCode::UnknownError );
		m_valid = false;
	}

#else 
	m_lastError = DetectionError(
		QStringLiteral( "[GpuDetector]: 未开启CUDA支持，无法使用GPU检测器" ),
		DetectionErrorCategory::CUDA,
		DetectionErrorCode::FallbackRequested );
#endif
}

Ort::SessionOptions GpuDetector::CreateSessionOptions( int threads ) {
	Ort::SessionOptions sessionOptions;
	// 设置线程数
	sessionOptions.SetIntraOpNumThreads( threads );
	// 创建CUDA EP
	OrtCUDAProviderOptions cuda_opt;
	// 绑定CUDAStream
	cuda_opt.device_id = CudaDevice::instance().deviceId();
	cuda_opt.has_user_compute_stream = 1;
	cuda_opt.user_compute_stream = CudaDevice::instance().stream();

	sessionOptions.AppendExecutionProvider_CUDA( cuda_opt );
	return sessionOptions;
}

bool GpuDetector::isValid()const {
	return m_valid;
}

const DetectionError& GpuDetector::lastError() const {
	return m_lastError;
}

GpuDetector::~GpuDetector() {
	
}

bool GpuDetector::infer( const GpuPreprocessResult& preprocessed, std::vector<float>& outData, std::vector<int64_t>& outShape ) {
#ifdef YOLO_HAVE_CUDA
	if (!m_valid || cudaDeviceId == -1 || !session) {
		m_lastError = DetectionError(
			QStringLiteral( "[GpuDetector]: GPU推理器未初始化/不支持CUDA/会话无效" ),
			DetectionErrorCategory::CUDA,
			DetectionErrorCode::FallbackRequested );
		return false;
	}
	// 校验预处理结果
	if (!preprocessed.isValid()) {
		m_lastError = DetectionError(
			QStringLiteral( "[GpuDetector]: 预处理结果无效" ),
			DetectionErrorCategory::Preprocess,
			DetectionErrorCode::DetectionFailed );
		return false;
	}
	// 校验配置
	if (!m_config.isValid()) {
		m_lastError = DetectionError(
			QStringLiteral( "[GpuDetector]: 配置无效" ),
			DetectionErrorCategory::Config,
			DetectionErrorCode::InvalidConfig );
		return false;
	}
	// 校验输入尺寸
	if (m_config.input_width <= 0 || m_config.input_height <= 0) {
		m_lastError = DetectionError(
			QStringLiteral( "[GpuDetector]: 输入尺寸无效: " ) + QString::number( m_config.input_width ) + " x " + QString::number( m_config.input_height ),
			DetectionErrorCategory::Config,
			DetectionErrorCode::InvalidConfig );
		return false;
	}
	// 进行推理
	try {
		// 创建输入张量(GPU)
		Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
			cuda_mem_info,
			preprocessed.gpuData,          // 显存指针
			3ll * m_inputWidth * m_inputHeight,
			m_inputShape.data(),
			m_inputShape.size() );
		// iobinding
		Ort::IoBinding io_binding( session );
		io_binding.BindInput( inputName.toUtf8().constData(), input_tensor );
		io_binding.BindOutput( outputName.toUtf8().constData(), mem_info ); // 绑定输出到CPU内存方便进行CPU后处理

		// 进行推理
		session.Run( Ort::RunOptions{ nullptr }, io_binding );

		// 获取输出结果
		std::vector<Ort::Value> ouput_tensors = io_binding.GetOutputValues();
		if (ouput_tensors.empty()) {
			m_lastError = DetectionError(
				QStringLiteral( "[GpuDetector]: 推理输出为空" ),
				DetectionErrorCategory::Other,
				DetectionErrorCode::UnknownError );
			return false;
		}
		// 将输出张量拷贝到CPU内存并进行出参
		auto output_shape = ouput_tensors[0].GetTensorTypeAndShapeInfo().GetShape();
		const size_t output_size = ouput_tensors[0].GetTensorTypeAndShapeInfo().GetElementCount();
		outShape.assign( output_shape.begin(), output_shape.end() );
		const float* output_data = ouput_tensors[0].GetTensorData<float>();
		outData.assign( output_data, output_data + output_size );
		return true;
	}
	catch (Ort::Exception& e) {
		m_lastError = DetectionError(
			QStringLiteral( "[GpuDetector]: 推理失败: " ) + QString::fromStdString( e.what() ),
			DetectionErrorCategory::Detection,
			DetectionErrorCode::DetectionFailed );
		return false;
	}
	catch (std::exception& e) {
		m_lastError = DetectionError(
			QStringLiteral( "[GpuDetector]: 推理失败: " ) + QString::fromStdString( e.what() ),
			DetectionErrorCategory::Detection,
			DetectionErrorCode::DetectionFailed );
		return false;
	}
	catch (...) {
		m_lastError = DetectionError(
			QStringLiteral( "[GpuDetector]: 推理失败: 未知异常" ),
			DetectionErrorCategory::Detection,
			DetectionErrorCode::DetectionFailed );
		return false;
	}

	// 如果没有开启CUDA，直接返回并fallback
#else	
	m_lastError = DetectionError(
		QStringLiteral( "[GpuDetector]: 设备暂不支持GPU推理功能，请求回退到CPU推理" ),
		DetectionErrorCategory::CUDA,
		DetectionErrorCode::FallbackRequested );
	return false;
#endif
}


bool GpuDetector::warmup( int iterations ) {
#ifdef YOLO_HAVE_CUDA
	if (!m_valid || cudaDeviceId == -1 || iterations <= 0 || !session) {
		m_warnings.emplace_back( QStringLiteral( "[GpuDetector]: GPU推理器未初始化/不支持CUDA/预热次数无效/会话无效" ), DetectorWarning::Code::WarmUpFailed );
		return false;
	}
	try {
		// CUDA内存释放器
		struct gpuBufferDeleter {
			float* buffer{nullptr};
			gpuBufferDeleter(const gpuBufferDeleter&) = delete;
			gpuBufferDeleter& operator=(const gpuBufferDeleter&) = delete;
			gpuBufferDeleter(float* ptr) : buffer(ptr) {}
			~gpuBufferDeleter() {
				if (buffer) {
					cudaFree( buffer );
					buffer = nullptr;
				}
			}
		};
		// 获取临时的显存
		float* temp_gpu_buffer = nullptr;
		size_t temp_buffer_size = 3ll * m_inputWidth * m_inputHeight * sizeof( float );
		auto ret = cudaMalloc( &temp_gpu_buffer, temp_buffer_size );
		gpuBufferDeleter deleter{ temp_gpu_buffer };
		if (ret != cudaSuccess) {
			m_warnings.emplace_back( QStringLiteral( "[GpuDetector]: 预热时无法分配显存" ), DetectorWarning::Code::WarmUpFailed );
			return false;
		}
		ret = cudaMemsetAsync( temp_gpu_buffer, 0, temp_buffer_size, CudaDevice::instance().stream() );
		if (ret != cudaSuccess) {
			m_warnings.emplace_back( QStringLiteral( "[GpuDetector]: 预热时无法初始化显存到设备共享流中" ), DetectorWarning::Code::WarmUpFailed );
			return false;
		}
		for (int i = 0;i < iterations;++i)
		{
			// 创建一个假的输入张量
			Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
				cuda_mem_info,
				temp_gpu_buffer,
				3ll * m_inputWidth * m_inputHeight,
				m_inputShape.data(),
				m_inputShape.size() );
			// iobinding
			Ort::IoBinding io_binding( session );
			io_binding.BindInput( inputName.toUtf8().constData(), input_tensor );
			io_binding.BindOutput( outputName.toUtf8().constData(), mem_info ); // 绑定输出到CPU内存方便进行CPU后处理
			// 进行推理
			session.Run( Ort::RunOptions{ nullptr }, io_binding );
			// 获取输出结果
			std::vector<Ort::Value> ouput_tensors = io_binding.GetOutputValues();
			if (ouput_tensors.empty()) {
				m_warnings.emplace_back( QStringLiteral( "[GpuDetector]: 预热时输出为空" ), DetectorWarning::Code::WarmUpFailed );
				return false;
			}
		}
		return true;
	}
	catch (Ort::Exception& e)
	{
		m_warnings.emplace_back( QStringLiteral( "[GpuDetector]: 预热时发生异常: " ) + QString::fromStdString( e.what() ), DetectorWarning::Code::WarmUpFailed );
		return false;
	}
	catch (std::exception& e)
	{
		m_warnings.emplace_back( QStringLiteral( "[GpuDetector]: 预热时发生异常: " ) + QString::fromStdString( e.what() ), DetectorWarning::Code::WarmUpFailed );
		return false;
	}
	catch (...)
	{
		m_warnings.emplace_back( QStringLiteral( "[GpuDetector]: 预热时发生未知异常" ), DetectorWarning::Code::WarmUpFailed );
		return false;
	}
#else 
	m_warnings.emplace_back( QStringLiteral( "[GpuDetector]: 设备暂不支持GPU推理功能，请求回退到CPU推理" ), DetectorWarning::Code::WarmUpFailed );
	return false;
#endif
}

