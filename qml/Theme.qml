import QtQuick

// ============================================================================
//  主题
//  深浅色的“底色/文字色”来自应用调色板(在 main.cpp 中依据系统主题设置，
//  SystemPalette 会跟随 paletteChanged 自动刷新)，因此这里不再重复定义，
//  保证 QML 与所有原生控件(Button / SpinBox / TextArea ...)配色完全一致。
//  只有日志级别配色是自定义的高对比色。
// ============================================================================
QtObject {
	id: theme

	// 系统调色板(反映应用调色板，随系统主题切换自动更新)
	property SystemPalette sys: SystemPalette {}

	// 是否深色主题：用窗口背景色明度判断，避免依赖具体枚举名
	readonly property bool dark: sys.window.hsvValue < 0.5

	// ---- 基础色(直接取自应用调色板) ----
	readonly property color window:     sys.window			// 窗口背景
	readonly property color windowText: sys.windowText		// 窗口前景文字
	readonly property color base:       sys.base			// 输入框 / 显示区背景
	readonly property color text:       sys.text			// 输入框 / 显示区文字
	readonly property color panel:      sys.alternateBase	// 面板 / 侧边栏背景
	readonly property color border:     sys.mid				// 边框
	readonly property color accent:     sys.highlight		// 强调色
	readonly property color muted:      sys.placeholderText	// 弱化文字

	// ---- 日志级别配色(自定义高对比色) ----
	readonly property color logDebug:   dark ? "#c9a7ff" : "#6b3fbf"	// 紫
	readonly property color logInfo:    dark ? "#7fb8ff" : "#0b5cad"	// 蓝
	readonly property color logSuccess: dark ? "#7bd88f" : "#1a7f37"	// 绿
	readonly property color logWarning: dark ? "#ffb454" : "#a05a00"	// 橙
	readonly property color logError:   dark ? "#ff6b6b" : "#c02626"	// 红
	readonly property color logFatal:   dark ? "#ff4d4d" : "#8b0000"	// 深红

	// 按级别取颜色(数值与 C++ Logger::Level 保持一致)
	function levelColor(level) {
		switch (level) {
		case 0: return logDebug
		case 1: return logInfo
		case 2: return logSuccess
		case 3: return logWarning
		case 4: return logError
		case 5: return logFatal
		default: return windowText
		}
	}
}
