#include "Detector.h"
#include <QDebug>
#include <QElapsedTimer>
#include <vector>
#include <span>
Detector::Detector()
	: env(ORT_LOGGING_LEVEL_WARNING, "Detector"),
	session{ nullptr },
	mem_info(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault))
{
}
Detector::Detector(const DetectorConfig& config_)
	: config{ config_ },
	env(ORT_LOGGING_LEVEL_WARNING, "Detector"),
	session{ nullptr },
	mem_info(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)),
	input_shape({ 1, 3, config_.input_height, config_.input_width })
{
	if (!std::filesystem::exists(config_.model_path)) {
		m_lastError = DetectorError(
			QStringLiteral("[Detector]: 模型路径无效: ") + QString::fromStdU16String(config_.model_path.u16string()),
			DetectorErrorCategory::Model,
			DetectorErrorCode::InvalidModelPath);
		return;	// 路径无效就不再尝试建会话
	}

	try {
		Ort::SessionOptions session_options =
			CreateSessionOptions(config.num_threads, config.use_cuda, config.device_id, cuda_enabled);

		session = Ort::Session(env, config.model_path.c_str(), session_options);

		Ort::AllocatorWithDefaultOptions allocator;
		input_name = QString::fromUtf8(session.GetInputNameAllocated(0, allocator).get());
		output_name = QString::fromUtf8(session.GetOutputNameAllocated(0, allocator).get());

		// 读取模型真实输入 shape并校正配置中的输入尺寸
		m_model_input_shape = session.GetInputTypeInfo(0)
			.GetTensorTypeAndShapeInfo().GetShape();
		alignInputSizeToModel();		 // 校正

		m_valid = true;		// 建立成功
	}
	catch (const std::exception& e) {
		m_lastError = DetectorError(
			QStringLiteral("[Detector]: 初始化检测器失败: ") + e.what(),
			DetectorErrorCategory::Other,
			DetectorErrorCode::UnknownError);
	}
}
const char* Detector::device() const {
	return cuda_enabled ? "CUDA" : "CPU";
}
bool Detector::usingCUDA() const {
	return cuda_enabled;
}
// 更改配置
bool Detector::setConfig(const DetectorConfig& config_) {
	if (!config_.isValid()) {
		m_lastError = DetectorError(
			QStringLiteral("[Detector]: 配置无效: 模型路径: ")+QString::fromStdString(config_.model_path.string())
			+QStringLiteral(" 类别数: ")+QString::number(config_.class_names.size())
			+QStringLiteral(" 置信度阈值: ")+QString::number(config_.conf_threshold) +QStringLiteral(" NMS阈值: ")+QString::number(config_.iou_threshold)
			+QStringLiteral(" 推理输入(W*H): ")
			+QString::number(config_.input_width) + QStringLiteral("*") + QString::number(config_.input_height)
			+QStringLiteral(" 线程数: ")+QString::number(config_.num_threads)
			+QStringLiteral(" 是否启用CUDA: ")
			+ QString(config_.use_cuda?"是":"否")
			+ QStringLiteral(" CUDA设备号: ")+QString::number(config_.device_id),
			DetectorErrorCategory::Config,
			DetectorErrorCode::InvalidConfig);
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
void Detector::alignInputSizeToModel() {
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
		m_warnings.push_back(DetectorWarning(
			QStringLiteral("[Detector]: 模型为动态输入(H/W 含 -1)，采用配置尺寸 ")
				+ QString::number(config.input_width) + QStringLiteral("×") + QString::number(config.input_height),
			DetectorWarning::Code::DynamicInputUsed,
			config.input_width, config.input_height,
			config.input_width, config.input_height));
		input_shape = { 1, 3, config.input_height, config.input_width };
		return;
	}

	// 静态尺寸且与配置不符：自动对齐 + 警告
	if (model_w != config.input_width || model_h != config.input_height) {
		const int to_w = static_cast<int>(model_w);
		const int to_h = static_cast<int>(model_h);
		m_warnings.push_back(DetectorWarning(
			QStringLiteral("[Detector]: 模型输入为 ") + QString::number(to_w) + QStringLiteral("×") + QString::number(to_h)
				+ QStringLiteral("，与配置的 ") + QString::number(config.input_width) + QStringLiteral("×") + QString::number(config.input_height)
				+ QStringLiteral(" 不一致，已自动按模型尺寸对齐"),
			DetectorWarning::Code::InputSizeAligned,
			config.input_width, config.input_height,
			to_w, to_h));
		config.input_width = to_w;
		config.input_height = to_h;
	}

	input_shape = { 1, 3, config.input_height, config.input_width };
}

// letterbox预处理并返回处理信息
cv::Mat Detector::letterbox(const cv::Mat& src, LetterboxInfo& info, int dst_w, int dst_h, cv::Scalar color, int interp) {
	// 检查图像是否合法
	if (src.empty() || src.channels() != 3) {
		m_lastError = DetectorError(
			QStringLiteral("[Detector]: 输入图像无效: H:") + QString::number(src.rows) + QStringLiteral(", W:") + QString::number(src.cols) + QStringLiteral(", channels: ") + QString::number(src.channels()),
			DetectorErrorCategory::Image,
			DetectorErrorCode::InvalidImage);
		return cv::Mat();
	}
	// 获取缩放比例(以最小为准,防止越界)以及新图的尺寸
	int src_w = src.cols, src_h = src.rows;
	float scale = std::min(static_cast<float>(dst_w) / src_w, static_cast<float>(dst_h) / src_h);
	int new_w = static_cast<int>(std::round(src_w * scale));
	int new_h = static_cast<int>(std::round(src_h * scale));
	new_w = std::max(1, std::min(new_w, dst_w));
	new_h = std::max(1, std::min(new_h, dst_h));
	// 将原图缩放到新尺寸
	cv::Mat resized;
	cv::resize(src, resized, cv::Size(new_w, new_h), 0, 0, interp);
	// 对缩放后的图像进行填充
	float pad_x = (dst_w - new_w) / 2.0f;
	float pad_y = (dst_h - new_h) / 2.0f;
	cv::Mat padded;
	cv::Mat canvas(dst_h, dst_w, src.type(), color);
	int ix = static_cast<int>(std::round(pad_x));
	int iy = static_cast<int>(std::round(pad_y));
	resized.copyTo(canvas(cv::Rect(ix, iy, new_w, new_h)));

	// 回填信息
	info.new_h = new_h;
	info.new_w = new_w;
	info.orig_h = src_h;
	info.orig_w = src_w;
	info.pad_h = static_cast<int>(std::round(pad_y));
	info.pad_w = static_cast<int>(std::round(pad_x));
	info.scale = scale;

	return canvas;
}

// 颜色转换并归一化并转换为NCHW布局
bool Detector::preprocess(const cv::Mat& src, cv::Mat& chw, int dst_w, int dst_h) {
	if (src.empty() || src.channels() != 3) {
		m_lastError = DetectorError(
			QStringLiteral("[Detector]: 输入图像无效: H:") + QString::number(src.rows) + QStringLiteral(", W:") + QString::number(src.cols) + QStringLiteral(", channels: ") + QString::number(src.channels()),
			DetectorErrorCategory::Image,
			DetectorErrorCode::InvalidImage);
		return false;
	}
	// swapRB=true 让 blobFromImage 内部完成 BGR->RGB 并归一化到 [0,1]，同时输出 NCHW。
	cv::dnn::blobFromImage(src, chw, 1.0 / 255.0f, cv::Size(dst_w, dst_h), cv::Scalar(), true, false, CV_32F);
	return true;
}

// 解析结果
// 内部全程用 std::vector：OpenCV 的 NMSBoxes 直接吃 std::vector，
// 省掉原先 QVector -> std::vector -> QVector 的两趟适配拷贝。
std::vector<Box> Detector::parse_yolo_ouput(float* data, int num_classes, int num_anchors)const {
	std::vector<Box> boxes;
	if (!data || num_classes <= 0 || num_anchors <= 0)
		return boxes;
	// YOLO 输入 (1,4+num_classes,num_anchors)的张量
	// num_anchors 个位置寻找合适的预测框
	for (int i = 0; i < num_anchors;++i) {
		float* row = data + i;
		float x = row[0 * num_anchors];
		float y = row[1 * num_anchors];
		float w = row[2 * num_anchors];
		float h = row[3 * num_anchors];

		// 找出置信度最大的类别
		float best_conf = 0.0f;
		int best_cls = -1;
		for (int c = 0;c < num_classes;++c) {
			float conf = data[num_anchors * (4 + c) + i];
			if (conf > best_conf) {
				best_conf = conf;
				best_cls = c;
			}
		}
		// 保留置信度大于阈值的框
		if (best_conf > config.conf_threshold)
			boxes.push_back({ x, y, w, h, best_conf, best_cls });
	}
	// 找出所有预测框后进行后处理
	// 1. NMS
	std::vector<cv::Rect> rects;
	std::vector<float> confidences; // 保存置信度
	rects.reserve(boxes.size());
	confidences.reserve(boxes.size());
	for (const auto& box : boxes) {// 将Box转换为cv::Rect
		rects.push_back(Box::Box2Rect(box, config.input_width, config.input_height));
		confidences.push_back(box.conf);
	}
	std::vector<int> indices; // 保存NMS后的索引
	cv::dnn::NMSBoxes(rects, confidences, config.conf_threshold, config.iou_threshold, indices);	 // NMS处理
	// 2. 根据indices保留筛选后的boxes(就地压缩，避免再开一个容器)
	std::vector<Box> filtered_boxes;
	filtered_boxes.reserve(indices.size());
	for (int i : indices) {
		filtered_boxes.push_back(boxes[static_cast<size_t>(i)]);
	}
	return filtered_boxes;
}

// 将预测框映射回原图
// letterbox 是等比缩放，所以必须先减去 padding 偏移，再统一除以 scale；
QVector<Box>Detector::map_boxes_to_original_image(std::span<const Box> boxes, const LetterboxInfo& info) const {
	QVector<Box>mapped_boxes;
	mapped_boxes.reserve(static_cast<qsizetype>(boxes.size()));
	const float inv_scale = 1.0f / info.scale;
	const float max_x = static_cast<float>(info.orig_w);
	const float max_y = static_cast<float>(info.orig_h);

	for (const auto& box : boxes) {
		// 中心点：先反平移，再反缩放
		float x = (box.x - static_cast<float>(info.pad_w)) * inv_scale;
		float y = (box.y - static_cast<float>(info.pad_h)) * inv_scale;
		float w = box.w * inv_scale;
		float h = box.h * inv_scale;

		// 裁剪到原图范围后重新计算中心点与宽高
		float x1 = std::max(0.0f, std::min(x - w / 2.0f, max_x));
		float y1 = std::max(0.0f, std::min(y - h / 2.0f, max_y));
		float x2 = std::max(0.0f, std::min(x + w / 2.0f, max_x));
		float y2 = std::max(0.0f, std::min(y + h / 2.0f, max_y));

		mapped_boxes.push_back({ (x1 + x2) / 2.0f, (y1 + y2) / 2.0f, x2 - x1, y2 - y1, box.conf, box.cls });
	}
	return mapped_boxes;
}
// 替换
void Detector::map_boxes_to_original_image_replace(std::span<Box> boxes, const LetterboxInfo& info) {
	const float inv_scale = 1.0f / info.scale;
	const float max_x = static_cast<float>(info.orig_w);
	const float max_y = static_cast<float>(info.orig_h);
	for (auto& box : boxes) {
		// 中心点：先反平移，再反缩放
		float x = (box.x - static_cast<float>(info.pad_w)) * inv_scale;
		float y = (box.y - static_cast<float>(info.pad_h)) * inv_scale;
		float w = box.w * inv_scale;
		float h = box.h * inv_scale;
		// 裁剪到原图范围后重新计算中心点与宽高
		float x1 = std::max(0.0f, std::min(x - w / 2.0f, max_x));
		float y1 = std::max(0.0f, std::min(y - h / 2.0f, max_y));
		float x2 = std::max(0.0f, std::min(x + w / 2.0f, max_x));
		float y2 = std::max(0.0f, std::min(y + h / 2.0f, max_y));
		box.x = (x1 + x2) / 2.0f;
		box.y = (y1 + y2) / 2.0f;
		box.w = x2 - x1;
		box.h = y2 - y1;
	}
}

// 推理检测目标
DetectionResult Detector::detect(const QString& image_path) {
	// 加载图片
	cv::Mat image = cv::imread(image_path.toStdString());
	if (image.empty()) {
		DetectorError error(QStringLiteral("[Detector]: 打开图片失败: ") + image_path, DetectorErrorCategory::Image, DetectorErrorCode::InvalidImage);
		m_lastError = error;
		return DetectionResult{ {},error ,0 };
	}
	return detect(image);
}

DetectionResult Detector::detect(const cv::Mat& imag) {
	cv::Mat image = imag.clone();
	if (image.empty()) {
		DetectorError error(QStringLiteral("[Detector]: 打开图片失败,图片无效!"), DetectorErrorCategory::Image, DetectorErrorCode::InvalidImage);
		m_lastError = error;
		return DetectionResult{ {},error ,0 };
	}
	if (!config.isValid()) {
		DetectorError error(QStringLiteral("[Detector]: 配置无效!"), DetectorErrorCategory::Config, DetectorErrorCode::InvalidConfig);
		m_lastError = error;
		return DetectionResult{ {},error ,0 };
	}
	// 预处理
	// 1.letterbox
	LetterboxInfo info;
	cv::Mat blob = letterbox(image, info, config.input_width, config.input_height);
	if (blob.empty())	// letterbox 内部已记录错误
		return DetectionResult{ {}, m_lastError, 0 };

	// 2. HWC -> NCHW
	cv::Mat chw;
	if (!preprocess(blob, chw, config.input_width, config.input_height))
		return DetectionResult{ {}, m_lastError, 0 };
	if (config.input_width <= 0 || config.input_height <= 0) {
		DetectorError error(QStringLiteral("[Detector]: 输入尺寸非法: ")
			+ QString::number(config.input_width) + QStringLiteral("×") + QString::number(config.input_height),
			DetectorErrorCategory::Config, DetectorErrorCode::InvalidConfig);
		m_lastError = error;
		return DetectionResult{ {}, error, 0 };
	}
	const qint64 need = 3ll * config.input_width * config.input_height;	// NCHW: 1*3*W*H
	if (chw.empty() || static_cast<qint64>(chw.total()) != need || !chw.isContinuous()) {
		DetectorError error(QStringLiteral("[Detector]: 转换为 NCHW 格式失败，当前尺寸:") + QString::number(config.input_width) + "x" + QString::number(config.input_height), DetectorErrorCategory::Other, DetectorErrorCode::UnknownError);
		m_lastError = error;
		return DetectionResult{ {},error,0 };
	}

	// ONNX 推理：异常在 runInference 内部兜住，绝不外抛
	return runInference(chw, info);
}

// 推理入口
DetectionResult Detector::runInference(cv::Mat& chw, const LetterboxInfo& info) {
	try {
		// 1. 构造输入tensor
		Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
			mem_info,
			chw.ptr<float>(),
			chw.total(),
			input_shape.data(),
			static_cast<size_t>(input_shape.size())
		);
		// 2. 推理
		//    必须使用 IoBinding 把输出显式绑定到 CPU 内存：
		//    启用 CUDA EP 后，默认输出张量位于显存，直接 GetTensorMutableData<float>()
		//    拿到的是设备指针，在 CPU 上解引用会读到垃圾数据甚至直接崩溃。
		//    绑定到 CPU 后 ORT 会自动完成 D2H 拷贝，CPU / CUDA 两种路径代码完全一致。

		Ort::MemoryInfo cpu_out_mem =
			Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeDefault);
		Ort::IoBinding io_binding{ session };
		io_binding.BindInput(input_name.toUtf8().constData(), input_tensor);
		io_binding.BindOutput(output_name.toUtf8().constData(), cpu_out_mem);

		QElapsedTimer timer; // 开始计时
		timer.start();
		session.Run(Ort::RunOptions{ nullptr }, io_binding);
		const qint64 duration = timer.elapsed(); // 获取延时(ms)

		std::vector<Ort::Value> output_tensors = io_binding.GetOutputValues();
		if (output_tensors.empty()) {
			DetectorError error(QStringLiteral("[Detector]: 未返回输出张量"), DetectorErrorCategory::Other, DetectorErrorCode::UnknownError);
			m_lastError = error;
			return { {},error,0 };
		}

		// 解析输出
		float* output_data = output_tensors[0].GetTensorMutableData<float>();
		auto output_shape = output_tensors[0].GetTensorTypeAndShapeInfo().GetShape();

		// 输出布局约定为 [1, 4+num_classes, num_anchors]。
		if (output_shape.size() < 3) {
			DetectorError error(QStringLiteral("[Detector]: 输出张量维度异常，期望 3 维，实际 ")
				+ QString::number(output_shape.size()), DetectorErrorCategory::Detection, DetectorErrorCode::DetectionFailed);
			m_lastError = error;
			return { {},error,0 };
		}
		const int num_classes = static_cast<int>(output_shape[1]) - 4;
		const int num_anchors = static_cast<int>(output_shape[2]);
		if (num_classes <= 0 || num_anchors <= 0) {
			DetectorError error(QStringLiteral("[Detector]: 输出张量形状异常: [")
				+ QString::number(output_shape[1]) + QStringLiteral(", ") + QString::number(output_shape[2])
				+ QStringLiteral("]，无法解析类别数与锚点数"), DetectorErrorCategory::Detection, DetectorErrorCode::DetectionFailed);
			m_lastError = error;
			return { {},error,0 };
		}

		std::vector<Box>boxes = parse_yolo_ouput(output_data, num_classes, num_anchors);
		// 就地映射回原图坐标：span 直接借用 vector 的连续存储，不发生拷贝
		map_boxes_to_original_image_replace( boxes, info );
		qInfo() << "[Detector]: 检测到" << boxes.size() << "个目标，推理耗时:" << duration << "ms";
		// 仅在返回给 Qt 结果的边界上构造 QVector<Box>
		return DetectionResult{ QVector<Box>(boxes.begin(), boxes.end()),
								DetectorError{QStringLiteral(""),DetectorErrorCategory::None,DetectorErrorCode::None},
								duration};
	}
	catch (const Ort::Exception& e) {
		m_lastError = DetectorError(QStringLiteral("[Detector]: ONNX 推理失败: ") + QString::fromUtf8(e.what()),
			DetectorErrorCategory::Detection, DetectorErrorCode::DetectionFailed);
		return DetectionResult{ {}, m_lastError, 0 };
	}
	catch (const cv::Exception& e) {
		m_lastError = DetectorError(QStringLiteral("[Detector]: OpenCV 异常: ") + QString::fromUtf8(e.what()),
			DetectorErrorCategory::Detection, DetectorErrorCode::DetectionFailed);
		return DetectionResult{ {}, m_lastError, 0 };
	}
	catch (const std::exception& e) {
		m_lastError = DetectorError(QStringLiteral("[Detector]: 推理异常: ") + QString::fromUtf8(e.what()),
			DetectorErrorCategory::Other, DetectorErrorCode::UnknownError);
		return DetectionResult{ {}, m_lastError, 0 };
	}
	catch (...) {
		m_lastError = DetectorError(QStringLiteral("[Detector]: 推理时发生未知异常"),
			DetectorErrorCategory::Other, DetectorErrorCode::UnknownError);
		return DetectionResult{ {}, m_lastError, 0 };
	}
}

// 预热：用一张假图跑若干次 Run
bool Detector::warmup(int iterations) {
	if (!m_valid || !session || iterations <= 0)
		return false;

	try {
		// 用与真实推理完全一致的尺寸(已按模型对齐过)构造假输入
		cv::Mat dummy(config.input_height, config.input_width, CV_8UC3, cv::Scalar(114, 114, 114));
		cv::Mat chw;
		if (!preprocess(dummy, chw, config.input_width, config.input_height))
			return false;

		QElapsedTimer timer;
		for (int i = 0; i < iterations; ++i) {
			timer.restart();
			Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
				mem_info, chw.ptr<float>(), chw.total(),
				input_shape.data(), static_cast<size_t>(input_shape.size()));

			// 与 runInference 保持同一条 IoBinding 路径，
			Ort::MemoryInfo cpu_out_mem =
				Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeDefault);
			Ort::IoBinding io_binding{ session };
			io_binding.BindInput(input_name.toUtf8().constData(), input_tensor);
			io_binding.BindOutput(output_name.toUtf8().constData(), cpu_out_mem);

			session.Run(Ort::RunOptions{ nullptr }, io_binding);
			io_binding.GetOutputValues();	// 必须取走输出，否则输出缓冲不参与热机

			qInfo() << "[Detector]: 预热" << (i + 1) << "/" << iterations
				<< "耗时" << timer.elapsed() << "ms";
		}
		return true;
	}
	catch (const std::exception& e) {
		qWarning() << "[Detector]: 预热失败(不影响后续检测):" << e.what();
		return false;
	}
	catch (...) {
		qWarning() << "[Detector]: 预热失败(未知异常，不影响后续检测)";
		return false;
	}
}

// 创建会话选项
// cuda_ok 作为出参：只有真正 Append 成功才置 true，调用方据此判断实际运行设备
QVector<DetectionResult> Detector::detect_video(cv::VideoCapture& cap, std::atomic<bool>* stop) {
	if (!cap.isOpened()) {
		DetectorError error(QStringLiteral("[Detector]: 视频流未打开!"), DetectorErrorCategory::Detection, DetectorErrorCode::DetectionFailed);
		m_lastError = error;
		return {};
	}
	QVector<DetectionResult> results;
	cv::Mat frame;
	while (cap.read(frame)) {
		// 逐帧检查停止标志：摄像头流不会自然结束,以此进行中断
		if (stop && stop->load(std::memory_order_relaxed))
			break;
		DetectionResult result = detect(frame);
		if (!result.error.has_error())
			results.push_back(result);
	}
	return results;
}

Ort::SessionOptions Detector::CreateSessionOptions(int threads, bool use_cuda, int device_id, bool& cuda_ok) {
	Ort::SessionOptions session_options;
	session_options.SetIntraOpNumThreads(threads);
	cuda_ok = false;

	if (!use_cuda) {
		return session_options;
	}

	try {
		auto providers = Ort::GetAvailableProviders();

		if (std::find(providers.begin(), providers.end(),
			std::string("CUDAExecutionProvider")) == providers.end()) {
			qWarning() << "[Detector]: 未检测到 CUDAExecutionProvider，已回退到 CPU";
			qWarning() << "[Detector]: 常见原因：未安装 CUDA Toolkit / cuDNN，或其 bin 目录不在 PATH 中";
			return session_options;
		}

		OrtCUDAProviderOptions cuda_opts{};
		cuda_opts.device_id = device_id;
		// Heuristic 更适合在线推理
		cuda_opts.cudnn_conv_algo_search = OrtCudnnConvAlgoSearchHeuristic;
		cuda_opts.gpu_mem_limit = SIZE_MAX;
		cuda_opts.arena_extend_strategy = 0;
		session_options.AppendExecutionProvider_CUDA(cuda_opts);

		cuda_ok = true;
	}
	catch (const std::exception& e) {
		qWarning() << "[Detector]: CUDA provider 加载失败，已回退到 CPU:" << e.what();
	}

	return session_options;
}

// 推理视频
QVector<DetectionResult> Detector::detect_video(const QString& video_path, std::atomic<bool>* stop) {
	cv::VideoCapture cap(video_path.toStdString());
	if (!cap.isOpened()) {
		DetectorError error(QStringLiteral("[Detector]: 打开视频失败!"), DetectorErrorCategory::Other, DetectorErrorCode::UnknownError);
		m_lastError = error;
		return {};
	}
	return detect_video(cap, stop);
}

Detector::~Detector() {
}


