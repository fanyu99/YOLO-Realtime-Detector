import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// ============================================================================
//  摄像头选择对话框(模态)
//  选择一个摄像头设备编号，确定后调用 DetectionApp.onDetectCameraClicked(id)。
//  说明：设备自动枚举需要后端探测，这里先提供 0-9 的编号输入。
// ============================================================================
Dialog {
	id: dialog
	title: "选择摄像头"
	modal: true
	width: 340

	property var theme: null	// 主题对象(Theme.qml)
	property var app: null		// DetectionApp 实例

	// 居中显示
	x: parent ? Math.round((parent.width - width) / 2) : 0
	y: parent ? Math.round((parent.height - height) / 2) : 0

	background: Rectangle {
		color: dialog.theme ? dialog.theme.panel : "white"
		border.color: dialog.theme ? dialog.theme.border : "gray"
		border.width: 1
		radius: 6
	}

	contentItem: ColumnLayout {
		spacing: 10

		Label {
			text: "请选择摄像头设备编号"
			color: dialog.theme ? dialog.theme.windowText : "black"
		}

		RowLayout {
			Layout.fillWidth: true
			spacing: 8
			Label {
				text: "设备 id"
				Layout.preferredWidth: 60
				color: dialog.theme ? dialog.theme.windowText : "black"
			}
			SpinBox {
				id: cameraSpin
				from: 0; to: 9; value: 0
				editable: true
				Layout.fillWidth: true
			}
		}
	}

	footer: DialogButtonBox {
		Button {
			text: "取消"
			DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
		}
		Button {
			text: "确定"
			DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
		}
	}

	// 点击"确定"：把设备编号交给后端
	onAccepted: if (app) app.onDetectCameraClicked(cameraSpin.value)
}
