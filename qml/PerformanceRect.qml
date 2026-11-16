import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// ============================================================================
//  可复用性能指标方块：名称在上、值在下
//  大小由外部布局决定，内部随窗口自适应。
//  decimals = 0 时按整数显示(计数类)，>0 时按小数显示(耗时类)。
// ============================================================================
Rectangle {
	id: root

	property var theme: null			// 主题对象(由外部注入)
	property string label: ""			// 指标名
	property real value: 0.0			// 指标值
	property string unit: ""			// 单位(ms / fps 等，可为空)
	property int decimals: 2			// 小数位；0 表示整数
	property string caption: ""			// 补充说明(如"最大 12.3 ms · 45 次")，空则不占行
	property string textOverride: ""	// 非空时直接显示该文本(用于级别/模式等字符串型指标)

	radius: 6
	color: root.theme ? root.theme.base : "#f0f0f0"
	border.color: root.theme ? root.theme.border : "#ccc"
	border.width: 1

	// 悬停微亮
	MouseArea {
		anchors.fill: parent
		hoverEnabled: true
		onEntered: root.opacity = 0.85
		onExited: root.opacity = 1.0
	}

	ColumnLayout {
		anchors.fill: parent
		anchors.margins: 10
		spacing: 4

		Label {
			id: nameLabel
			Layout.fillWidth: true
			text: root.label
			font.pixelSize: 12
			opacity: 0.7
			elide: Text.ElideRight
			color: root.theme ? root.theme.windowText : "#333"
		}

		Label {
			Layout.fillWidth: true
			Layout.fillHeight: true
			verticalAlignment: Text.AlignVCenter
			text: root.textOverride.length > 0
				  ? root.textOverride
				  : Number(root.value).toFixed(root.decimals) + (root.unit.length > 0 ? " " + root.unit : "")
			font.pixelSize: 18
			font.bold: true
			elide: Text.ElideRight
			color: root.theme ? root.theme.accent : "#0078d7"
		}

		Label {
			visible: root.caption.length > 0
			Layout.fillWidth: true
			text: root.caption
			font.pixelSize: 11
			opacity: 0.6
			elide: Text.ElideRight
			color: root.theme ? root.theme.windowText : "#333"
		}
	}
}
