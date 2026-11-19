#pragma once
// 预览模式
enum class ViewMode {
	OffLine,		// 离线
	RealTime		 // 实时
};
// 后端显示接口
enum class DisplayBackend {
	Overlay, // GPU叠加层
	ImageProvider // CPU图片
};