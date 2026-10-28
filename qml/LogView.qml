import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// ============================================================================
//  日志面板
//  订阅 Logger 单例的 messageLogged 信号，按级别着色显示。
//  级别配色：Debug 紫 / Info 蓝 / Success 绿 / Warning 橙 / Error 红 / Fatal 深红
// ============================================================================
Item {
	id: root

	property var theme: null		// 由外部注入的主题对象(Theme.qml)

	// 外部传入的日志模型(可选)。为 null 时本组件自建模型并自行订阅 Logger；
	// 传入时(如性能面板有嵌入 / 分离两处视图)，由持有者负责填充，
	// 本组件只负责显示，两份视图共享同一份历史。
	property ListModel logModel: null

	// 日志上限，避免长时间运行内存无限增长(仅本组件自建模型时生效)
	readonly property int maxLines: 1000

	// 实际使用的模型：外部给的优先，否则用自建的
	readonly property ListModel activeModel: logModel ? logModel : internalLogModel

	// 日志数据模型：每条记录含 level / time / message
	ListModel { id: internalLogModel }

	ColumnLayout {
		anchors.fill: parent
		spacing: 4

		// 标题行
		RowLayout {
			Layout.fillWidth: true
			Label {
				text: "日志"
				font.bold: true
				Layout.fillWidth: true
			}
			ToolButton {
				text: "清空"
				onClicked: root.activeModel.clear()
			}
			// 推理日志输出间隔：逐帧推理的结果日志每隔 N 帧才落一条，
			// 避免视频 / 摄像头把日志面板刷爆。0 表示不间隔（每帧都输出）。
			// 值直接写回 Logger 单例，Detector 侧的 infoWithInterval 立即生效。
			Label {
				text: "间隔"
				opacity: 0.75
				font.pixelSize: 12
			}
			SpinBox {
				id: inferenceIntervalSpinBox
				from: 0
				to: 200
				stepSize: 1
				editable: true
				value: Logger.infoInterval()
				// 0 太长就显示成“每帧”，避免被窄列截断
				textFromValue: function(v) { return v <= 0 ? "每帧" : String(v) }
				valueFromText: function(t) {
					var n = parseInt(t, 10)
					return isNaN(n) ? inferenceIntervalSpinBox.value : n
				}
				onValueChanged: Logger.setInfoInterval(value)

				HoverHandler { id: intervalHover }
				ToolTip.visible: intervalHover.hovered
				ToolTip.delay: 600
				ToolTip.text: "推理结果日志每隔多少帧输出一条；0 = 每帧都输出"
			}
		}

		// 日志列表
		Rectangle {
			Layout.fillWidth: true
			Layout.fillHeight: true
			color: root.theme ? root.theme.base : "white"
			border.color: root.theme ? root.theme.border : "gray"
			border.width: 1
			radius: 4
			clip: true

			ListView {
				id: logList
				anchors.fill: parent
				anchors.margins: 6
				model: root.activeModel
				clip: true
				spacing: 1

				// 有新日志时自动滚到底部
				onCountChanged: if (logList.count > 0) logList.positionViewAtEnd()

				delegate: Text {
					required property int level
					required property string time
					required property string message

					width: logList.width
					wrapMode: Text.Wrap
					font.family: "Consolas"
					font.pixelSize: 12
					color: root.theme ? root.theme.levelColor(level) : "black"
					text: "[" + time + "] " + message
				}
			}
		}
	}

	// 订阅日志信号：后端每写一条日志，这里就往模型里追加一行。
	// 模型由外部提供时不订阅，避免两份视图重复写入。
	Connections {
		target: Logger
		enabled: root.logModel === null
		function onMessageLogged(level, levelName, time, message) {
			internalLogModel.append({ "level": level, "time": time, "message": message })
			if (internalLogModel.count > root.maxLines)
				internalLogModel.remove(0, internalLogModel.count - root.maxLines)
		}
	}
}
