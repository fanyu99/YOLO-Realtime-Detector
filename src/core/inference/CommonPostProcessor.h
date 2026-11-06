#pragma once 
#include <span>
#include "DetectionTypes.h"
// 通用的后处理器，将推理结果进行NMS处理，最后将检测框返回
class CommonPostProcessor {
public:
	explicit CommonPostProcessor() = default;
	~CommonPostProcessor() = default;
	// 获取两个检测框之间的IoU
	float getIoU( const Box& b1, const Box& b2 )const;
	// NMS，需要保证已经有过置信度筛选
	// 内部进行排序（类别编号、置信度）
	std::vector<Box>NMS(std::span< Box>boxes,float iou_threshold);
	// 解析推理结果
	std::vector<Box>parse_yolo_output( float* data, int num_classes, int num_anchors, float conf_threshold ,float iou_threshold);
	// 将预测框从 letterbox 后的 640x640 空间映射回原图(替换原本的检测框)
	void  mapBoxes2OriImage_replace( std::span<Box> boxes, const LetterboxInfo& info );
	// 统一的推理后处理	,出现问题记录错误并返回false，成功返回true
	// 出参为out_boxes
	bool  postProcess( float* data, int num_classes, int num_anchors, const LetterboxInfo& info, float conf_threshold, float iou_threshold, std::vector<Box>& out_boxes );

	// 获取错误
	DetectionError lastError() const;
private:
	DetectionError m_lastError;	// 最近一次错误
};