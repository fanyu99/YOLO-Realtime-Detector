import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T

// ============================================================================
//  对话框「确定」的 Enter 快捷键。
//
//  统一替代各对话框里成对的 Shortcut：
// ============================================================================
Shortcut {
	id: acceptShortcut

	required property Dialog popup				// 所属对话框
	required property T.AbstractButton button	// 对话框里的「确定」按钮

	sequences: ["Return", "Enter"]
	enabled: popup.visible && button.enabled
	onActivated: button.click()
}
