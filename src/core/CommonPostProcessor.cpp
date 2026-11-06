#include "CommonPostProcessor.h"
#include <algorithm>
#include <cmath>
// 获取IoU
float CommonPostProcessor::getIoU( const Box& b1, const Box& b2 )const {
	float x1 = b1.x - b1.w / 2,	// 左上角坐标
		x2 = b2.x - b2.w / 2,
		y1 = b1.y - b1.h / 2,
		y2 = b2.y - b2.h / 2;
	float left = std::fmaxf( x1, x2 );
	float right = std::fminf( b1.x + b1.w / 2, b2.x + b2.w / 2 );
	float top = std::fmaxf( y1, y2 );
	float bottom = std::fminf( b1.y + b1.h / 2, b2.y + b2.h / 2 );
	float inter_area = (right - left >= 0 ? right - left : 0) * (bottom - top >= 0 ? bottom - top : 0);
	float area1 = b1.getArea();
	float area2 = b2.getArea();
	float union_area = area1 + area2 - inter_area;
	if (std::abs( union_area - 0.0f ) < 1e-6) {
		return 0.0f;
	}
	return inter_area / union_area;
}

// 同类别NMS  需要保证有过置信度筛选  时间复杂度O(n^2)
std::vector<Box>CommonPostProcessor::NMS( std::span<Box>boxes, float iou_threshold ) {
	// 按照类别和置信度排序，先按类别升序，再按置信度降序
	// nlogn排序
	std::sort( boxes.begin(), boxes.end(), [ ]( const Box& a, const Box& b ) {
		if (a.cls != b.cls) {
			return a.cls < b.cls;
		}
		return a.conf > b.conf;
		} );
	int n = static_cast<int>(boxes.size());
	std::vector<Box> filtered_boxes;
	filtered_boxes.reserve( n );
	// 用来判断是否被舍弃
	std::vector<bool> isDropped( n, false );
	// 最坏情况下全部都为同一类别的目标框，时间复杂度O(n^2)
	for (int begin = 0; begin < n;) {
		int end = begin + 1; // 当前的同类别置信框边界（开边界）
		// 找到同类别目标框范围
		while (end < n && 
			boxes[end].cls == boxes[begin].cls) {
			++end;
		}
		// 在同类别的范围内进行NMS
		for (int i = begin; i < end; ++i) {
			if (isDropped[i]) {	   // 舍弃就跳过
				continue;
			}
			// 没有被舍弃就加入到结果
			filtered_boxes.emplace_back( boxes[i] );
			// 在后续的同类别检测框中筛选IoU大于阈值的框舍弃
			for (int j = i + 1; j < end; ++j) {
				if (!isDropped[j] &&
					getIoU( boxes[i], boxes[j] ) > iou_threshold)
					isDropped[j] = true;	// 大于阈值舍弃
			}
		}
		begin = end;   // 移动到下一个类别
	}
	return filtered_boxes;
}


// 解析推理结果
std::vector<Box>CommonPostProcessor::parse_yolo_output( float* data, int num_classes, int num_anchors,float conf_threshold ,float iou_threshold) {
	// 筛选目标
	// 找出一个目标的检测中置信度最大的类别，如果大于置信度阈值就保留这个目标
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
		if (best_conf > conf_threshold)
			boxes.emplace_back( Box{ x, y, w, h, best_conf, best_cls } );
	}
	// NMS:对同类别中的目标框
	return NMS( boxes, iou_threshold );
}


// 将预测框从 letterbox 后的 640x640 空间映射回原图(替换原本的检测框)
void CommonPostProcessor::mapBoxes2OriImage_replace( std::span<Box> boxes, const LetterboxInfo& info ) {
	const float inv_scale = 1.0f / info.scale;  // 反比例
	const float max_x = static_cast<float>(info.orig_w);
	const float max_y = static_cast<float>(info.orig_h);
	for (auto& box : boxes) {
		// 中心点：先反平移，再反缩放
		float x = (box.x - static_cast<float>(info.pad_w)) * inv_scale;
		float y = (box.y - static_cast<float>(info.pad_h)) * inv_scale;
		float w = box.w * inv_scale;
		float h = box.h * inv_scale;
		// 裁剪到原图范围后重新计算中心点与宽高
		float x1 = std::max( 0.0f, std::min( x - w / 2.0f, max_x ) );
		float y1 = std::max( 0.0f, std::min( y - h / 2.0f, max_y ) );
		float x2 = std::max( 0.0f, std::min( x + w / 2.0f, max_x ) );
		float y2 = std::max( 0.0f, std::min( y + h / 2.0f, max_y ) );
		box.x = (x1 + x2) / 2.0f;
		box.y = (y1 + y2) / 2.0f;
		box.w = x2 - x1;
		box.h = y2 - y1;
	}
}


// 统一的推理后处理	,出现问题记录错误并返回false，成功返回true
bool CommonPostProcessor::postProcess( float* data, int num_classes, int num_anchors, const LetterboxInfo& info, float conf_threshold, float iou_threshold, std::vector<Box>& out_boxes ) {
	// 复位
	m_lastError = DetectionError();
	if (!data || num_classes <= 0 || num_anchors <= 0) {
		m_lastError = DetectionError(
			QStringLiteral( "[CommonPostProcessor]: 推理输出数据无效或类别数/锚点数非法: num_classes:" ) + QString::number( num_classes ) + QStringLiteral( ", num_anchors:" ) + QString::number( num_anchors ),
			DetectionErrorCategory::Postprocess,
			DetectionErrorCode::PostprocessFailed );
		return false;
	}
	if (info.orig_w <= 0 || info.orig_h <= 0 || info.new_w <= 0 || info.new_h <= 0) {
		m_lastError = DetectionError(
			QStringLiteral( "[CommonPostProcessor]: letterbox 信息非法: orig_w:" ) + QString::number( info.orig_w ) + QStringLiteral( ", orig_h:" ) + QString::number( info.orig_h )
			+ QStringLiteral( ", new_w:" ) + QString::number( info.new_w ) + QStringLiteral( ", new_h:" ) + QString::number( info.new_h ),
			DetectionErrorCategory::Postprocess,
			DetectionErrorCode::PostprocessFailed );
		return false;
	}
	out_boxes = parse_yolo_output( data, num_classes, num_anchors, conf_threshold, iou_threshold );
	mapBoxes2OriImage_replace( out_boxes, info );
	return true;
}
DetectionError  CommonPostProcessor::lastError() const {
	return m_lastError;
}