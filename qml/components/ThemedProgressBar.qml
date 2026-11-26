import QtQuick
import QtQuick.Controls

// 进度条
ProgressBar {
    id: root

    property var theme: null

    implicitWidth: background.implicitWidth
    implicitHeight: background.implicitHeight
    from: 0
    to: 1
    // 颜色变化
    function valueColor(v) {
        var p = v * 100;
        if (p < 20)
            return "#FF0000";
        if (p < 40)
            return "#FF00AA";
        if (p < 60)
            return "#0055FF";
        if (p < 80)
            return "#00DD99";
        return "#00E600";
    }

    background: Rectangle {
        implicitWidth: 200
        implicitHeight: 24
        radius: 4
        border.color: root.theme ? root.theme.border : "#808080"
        color: "#e0e0e0"
    }

    contentItem: Item {
        Rectangle {
            width: root.visualPosition * parent.width
            height: parent.height
            radius: 8
            gradient: Gradient {
                GradientStop {
                    position: 0.0
                    color: root.valueColor(root.value)
                }
                GradientStop {
                    position: 1.0
                    color: "#43a047"
                }
            }
        }
        // 进度显示百分比
        Text {
            anchors.centerIn: parent
            text: Math.round(root.value * 100) + "%"
            font.pixelSize: 13
            font.bold: true
            font.family: "Segoe UI, sans-serif"
            style: Text.Sunken
            styleColor: "#40ffffff"
        }
    }
}
