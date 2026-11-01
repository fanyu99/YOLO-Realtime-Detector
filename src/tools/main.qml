import QtQuick
import QtQuick.Controls
import QtQuick.Layouts


// rtspController 由 app_main.cpp 通过 context property 注入。
Window {
	id: window
	width: 1000
	height: 700
	minimumWidth: 640
	minimumHeight: 480
	visible: true
	title: "RTSP Puller"
	color: "#1e1e1e"

	ColumnLayout {
		anchors.fill: parent
		anchors.margins: 8
		spacing: 8

		RowLayout {
			Layout.fillWidth: true
			spacing: 8

			TextField {
				id: urlField
				Layout.fillWidth: true
				placeholderText: "请输入RTSP协议URL"
				selectByMouse: true
				onAccepted: startStream()
			}

			Button {
				text: "开始"
				onClicked: startStream()
			}
			Button {
				text: "暂停"
				onClicked: rtspController.pause()
			}
			Button {
				text: "恢复"
				onClicked: rtspController.resume()
			}
			Button {
				text: "停止"
				onClicked: rtspController.stop()
			}
			Button{
			text:rtspController.autoReconnect ? "关闭自动重连" : "开启自动重连"
			onClicked: rtspController.setAutoReconnect(!rtspController.autoReconnect)
			}
		}
		// 状态栏：显示当前状态、帧序号、错误信息、显示 FPS、解码 FPS
		RowLayout {
			Layout.fillWidth: true
			spacing: 12

			Label {
				text: "状态：" + rtspController.statusText
			}
			Label {
				text: "帧序号：" + rtspController.frameRevision
				opacity: 0.75
			}
			Label {
				Layout.fillWidth: true
				text: rtspController.currentError.getErrorMessage()
				color: "#ff8a80"
				elide: Text.ElideRight
			}
			Label{
				Layout.fillWidth: true
				text: "显示 FPS：" + rtspController.streamStatus.displayFps.toFixed(1)
				color: "#8a8a8a"
				elide: Text.ElideRight
			}
			Label{
				Layout.fillWidth: true
				text: "解码 FPS：" + rtspController.streamStatus.decodeFps.toFixed(1)
				color: "#8a8a8a"
				elide: Text.ElideRight
			}
		}
	
		// 状态栏2：显示解码帧数、丢帧数、丢包数、重连次数
		RowLayout{
			Layout.fillWidth: true
			spacing : 12
			Label {
				text: "接收包数: "+rtspController.streamStatus.receivedPacketCount
			}
			Label{
				text: "视频包数: "+rtspController.streamStatus.videoPacketCount
			}
			Label{
				text: "解码帧数: "+rtspController.streamStatus.decodedFrameCount
			}
			Label{
				text: "丢帧数: "+rtspController.streamStatus.droppedFrameCount
			}
			Label{
				text:"解码错误次数: "+rtspController.streamStatus.decodeErrorCount
			}
			Label{
			text: "重连次数: "+rtspController.streamStatus.reconnectCount
			}
			
		}

		// 画面预览区域
		Rectangle {
			Layout.fillWidth: true
			Layout.fillHeight: true
			color: "#161617"
			border.color: "#3d3d3d"
			border.width: 1
			clip: true

			Image {
				id: preview
				anchors.fill: parent
				anchors.margins: 4
				fillMode: Image.PreserveAspectFit
				cache: false
				asynchronous: false
				source: rtspController.frameRevision > 0
					? "image://rtsp/live?rev=" + rtspController.frameRevision
					: ""
			}

			Label {
				anchors.centerIn: parent
				text: "无画面"
				color: "#8a8a8a"
				visible: preview.status !== Image.Ready
			}
		}
	}

	function startStream() {
		rtspController.setUrl(urlField.text)
		rtspController.start()
	}
}
