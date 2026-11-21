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

    property ListModel logModel: null

    // 日志上限
    readonly property int maxLines: 1000
    property bool closeEnable: true

    // 使用的model
    readonly property ListModel activeModel: logModel ? logModel : internalLogModel

    // 每条记录含 level / time / message
    ListModel {
        id: internalLogModel
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 4

        // 标题行
        Frame {
            Layout.fillWidth: true
            Layout.minimumHeight: 40
            padding: 4

            RowLayout {
                anchors.fill: parent
                spacing: 8
                Item {
                    Layout.leftMargin: 2
                }
                Label {
                    text: "日志"
                    font.bold: true
                    font.pixelSize: 18
                }

                Item {
                    Layout.fillWidth: true
                }

                Label {
                    text: "信息间隔"
                    font.pixelSize: 12
                }
                SpinBox {
                    id: inferenceIntervalSpinBox
                    from: 0
                    to: 200
                    stepSize: 5
                    editable: true
                    value: Logger.infoInterval()
                    textFromValue: function (v) {
                        return v <= 0 ? "每帧" : String(v);
                    }
                    valueFromText: function (t) {
                        var n = parseInt(t, 30);
                        return isNaN(n) ? inferenceIntervalSpinBox.value : n;
                    }
                    onValueChanged: function () {
                        Logger.setInfoInterval(value);
                        Logger.info("日志间隔已经更新为：%1".arg(inferenceIntervalSpinBox.value));
                    }

                    HoverHandler {
                        id: intervalHover
                    }
                    ToolTip.visible: intervalHover.hovered
                    ToolTip.delay: 600
                    ToolTip.text: "推理结果日志每隔多少帧输出一条；为0每帧都输出"
                }

                Button {
                    Layout.fillHeight: true
                    text: "清空"
                    onClicked: root.activeModel.clear()
                    font.pixelSize: 12
                    flat: true
                }

                Button {
                    id: closeBtn
                    Layout.fillHeight: true
                    text: "关闭日志"
                    icon.color: "#b2ee0e0e"
                    onClicked: root.visible = false
                    font.pixelSize: 12
                    visible: root.closeEnable
                    flat: true
                }
                Item {
                    Layout.rightMargin: 2
                }
            }
        }

        // 日志列表

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 150
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
                onCountChanged: if (logList.count > 0)
                    logList.positionViewAtEnd()

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

    Connections {
        target: Logger
        enabled: root.logModel === null
        function onMessageLogged(level, levelName, time, message) {
            internalLogModel.append({
                "level": level,
                "time": time,
                "message": message
            });
            if (internalLogModel.count > root.maxLines)
                internalLogModel.remove(0, internalLogModel.count - root.maxLines);
        }
    }
}
