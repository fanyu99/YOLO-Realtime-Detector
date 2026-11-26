import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

//  状态横幅
//  级别：none / success / warning / error。
Rectangle {
    id: root

    property var theme: null
    property string text: ""
    property string level: "none"
    property int autoDismissMs: 5000		// <=0 不自动关闭

    signal dismissed() // 点击自动发出关闭信号，上层调用进行隐藏

    readonly property bool isSuccess: level === "success"
    readonly property bool isError: level === "error"

    implicitHeight: 34
    radius: 4
    color: isError
        ? (theme.dark ? "#3d0f0f" : "#fde3e3")
        : isSuccess
            ? (theme.dark ? "#0d3321" : "#e3f7ec")
            : (theme.dark ? "#3d3208" : "#fff4d6")
    border.color: isError ? theme.logError : isSuccess ? theme.logSuccess : theme.logWarning
    border.width: 1

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 6
        spacing: 8
        // 左边表示级别的色块
        Rectangle {
            Layout.preferredWidth: 4
            Layout.preferredHeight: 16
            radius: 2
            color: isError ? theme.logError : isSuccess ? theme.logSuccess : theme.logWarning
        }
        // 显示文字
        Label {
            Layout.fillWidth: true
            text: root.text
            elide: Text.ElideMiddle
            color: isError
                ? (theme.dark ? "#ffb0b0" : "#8a1a1a")
                : isSuccess
                    ? (theme.dark ? "#9fe1cb" : "#0f5132")
                    : (theme.dark ? "#ffd9a0" : "#7a4a00")
            font.pixelSize: 12
        }

        Button {
            text: "知道了"
            flat: true
            onClicked: root.dismissed()
        }

        // 成功级别自动关闭横幅
        Timer {
            interval: root.autoDismissMs
            running: root.visible && root.isSuccess && root.autoDismissMs > 0
            repeat: false
            onTriggered: root.dismissed()
        }
    }
}
