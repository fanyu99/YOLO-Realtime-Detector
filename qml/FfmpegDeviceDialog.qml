import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
// ============================================================================
//  ffmpeg摄像头选择对话框(模态)
//  列出 ffmpeg(dshow/avfoundation/v4l2) 枚举到的 video 输入设备，确定后调用
//  DetectionApp.onDetectFfmpegDeviceClicked(name) —— 和海康相机走的是
//  同一条提交路径(DetectionManager::submitSource)。
//
//  枚举是一次阻塞调用(dshow 可能数百毫秒)，所以只在对话框打开和点「刷新」时做一次
// ============================================================================

Dialog {
	id: dialog
	title: "[ffmpeg] 选择摄像头"
	modal: true
	width: 420

	property var theme: null	// 主题对象(Theme.qml)
	property var app: null		// DetectionApp 实例

	// 枚举结果，元素为 { name, id, isDefault }
	property var devices: []
	readonly property bool hasSelection: deviceList.currentIndex >= 0

	function currentDevice() {
		return hasSelection ? devices[deviceList.currentIndex] : null
	}

	function refresh() {
		devices = app ? app.ffmpegVideoDevices() : []
		// 优先选中默认设备，其次选中第一台
		let defaultIndex = -1
		for (let i = 0; i < devices.length; ++i)
			if (devices[i].isDefault) { defaultIndex = i; break }
		deviceList.currentIndex = devices.length > 0 ? (defaultIndex >= 0 ? defaultIndex : 0) : -1
	}

	// 每次打开都重新枚举
	onOpened: refresh()

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

		RowLayout {
			Layout.fillWidth: true
			spacing: 8

			Label {
				Layout.fillWidth: true
				text: dialog.devices.length > 0
					  ? "已发现 " + dialog.devices.length + " 台设备，请选择："
					  : "未发现设备"
				color: dialog.theme ? dialog.theme.windowText : "black"
			}

			ToolButton {
				text: "刷新"
				enabled: dialog.app !== null
				onClicked: dialog.refresh()
			}
		}

		ListView {
			id: deviceList
			Layout.fillWidth: true
			Layout.preferredHeight: 180
			clip: true
			spacing: 4
			visible: dialog.devices.length > 0
			model: dialog.devices

			delegate: Rectangle {
				required property int index
				required property var modelData

				width: ListView.view.width
				height: 52
				radius: 4
				color: dialog.theme ? dialog.theme.base : "white"
				border.width: ListView.isCurrentItem ? 2 : 1
				border.color: ListView.isCurrentItem
							  ? (dialog.theme ? dialog.theme.accent : "steelblue")
							  : (dialog.theme ? dialog.theme.border : "gray")

				Column {
					anchors.left: parent.left
					anchors.right: parent.right
					anchors.leftMargin: 10
					anchors.rightMargin: 10
					anchors.verticalCenter: parent.verticalCenter
					spacing: 2

					Label {
						width: parent.width
						elide: Text.ElideRight
						text: (modelData.name || "(未知设备)")
							  + (modelData.isDefault ? "　[默认]" : "")
						color: dialog.theme ? dialog.theme.windowText : "black"
					}

					Label {
						width: parent.width
						elide: Text.ElideRight
						// 原始标识（dshow 是 PNP 路径）很长，只在悬停时提示，列表里截断显示
						text: "标识：" + (modelData.id || "—")
						font.pixelSize: 11
						opacity: 0.75
						color: dialog.theme ? dialog.theme.muted : "gray"
					}
				}

				MouseArea {
					anchors.fill: parent
					onClicked: deviceList.currentIndex = index
					onDoubleClicked: {
						deviceList.currentIndex = index
						dialog.accept()			// 双击直接开始
					}
				}
			}
		}

		// 空态：把常见原因直接写出来
		Label {
			Layout.fillWidth: true
			Layout.preferredHeight: 180
			visible: dialog.devices.length === 0
			horizontalAlignment: Text.AlignHCenter
			verticalAlignment: Text.AlignVCenter
			wrapMode: Text.WordWrap
			color: dialog.theme ? dialog.theme.muted : "gray"
			text: "没有枚举到摄像头。\n请确认设备已连接、未被其他程序占用，然后点「刷新」。"
		}
	}

	footer: DialogButtonBox {
		Button {
			text: "刷新"
			DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
			onClicked: dialog.refresh()
		}
		Button {
			text: "取消"
			DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
		}
		Button {
			id: acceptButton
			text: "确定"
			enabled: dialog.hasSelection
			DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
		}
	}
	AcceptShortcut { popup: dialog; button: acceptButton }

	// 点击「确定」：把设备名交给后端
	onAccepted: {
		const device = currentDevice()
		if (device && dialog.app)
			dialog.app.onDetectFfmpegDeviceClicked(device.name)
	}
}
