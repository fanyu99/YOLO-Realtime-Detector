#pragma once
#include <QString>
#include <optional>
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

// 显示策略
// preferred_mode用户模式，active_mode实际生效模式（运行时填入），backend显示后端
struct DisplayPolicy {
	// 用户偏好
	ViewMode preferred_mode = ViewMode::OffLine;
	// 实际模式
	std::optional<ViewMode> active_mode = std::nullopt;
	// 后端显示模式
	DisplayBackend backend = DisplayBackend::ImageProvider;

	// 通过当前的模式推导显示后端
	static DisplayBackend backendFor ( ViewMode mode ) {
		return mode == ViewMode::RealTime ? DisplayBackend::Overlay : DisplayBackend::ImageProvider;
	}
	ViewMode preferredMode () const { return preferred_mode; }
	// 实际生效的模式：active_mode ，没有回退到preferredMode
	ViewMode activeMode () const { return active_mode.value_or ( preferred_mode ); }
	DisplayBackend displayBackend () const { return backend; }
	void setBackend ( DisplayBackend b ) { backend = b; }
	void setActiveMode ( ViewMode aMode ) { active_mode = aMode; }
	bool setViewMode ( ViewMode newMode ) {
		if (newMode == preferred_mode) {
			return false;
		}
		this->preferred_mode = newMode;
		this->active_mode = std::nullopt;		// 同步清空当前生效的模式
		this->backend = backendFor ( newMode );	// 后端重新推导
		return true;
	}
};

inline QString ViewMode2String ( ViewMode mode ) {
	switch (mode) {
	case ViewMode::OffLine:
		return "offline";
	case ViewMode::RealTime:
		return "realtime";
	default:
		return "Unknown";
	}
}

inline std::optional<ViewMode> String2ViewMode ( const QString& str ) {
	if (str == "offline") {
		return ViewMode::OffLine;
	} else if (str == "realtime") {
		return ViewMode::RealTime;
	} else {
		return std::nullopt;
	}
}

inline QString Backend2String ( DisplayBackend backend ) {
	switch (backend) {
	case DisplayBackend::Overlay:
		return "Overlay";
	case DisplayBackend::ImageProvider:
		return "ImageProvider";
	default:
		return "Unknown";
	}
}

