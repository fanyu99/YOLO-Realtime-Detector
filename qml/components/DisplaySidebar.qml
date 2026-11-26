pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

// ============================================================================
//  左侧常驻配置栏
//  当前配置 / 颜色-类别图例 / 结果保存 / 状态栏 / 检测控制。
//  theme 与检测后端由外部注入，组件不依赖全局单例名。
// ============================================================================
Frame {
    id: root

    property var theme: null
    property var app: null

    // ---- 状态栏(与日志同源：订阅 Logger.messageLogged) ----
    property int statusLevel: 1
    property string statusText: "就绪"
    property string statusTime: ""

    function localPathFromUrl(u) {
        var s = u.toString();
        if (s.startsWith("file:///"))
            s = decodeURIComponent(s.substring(8));
        else if (s.startsWith("file://"))
            s = decodeURIComponent(s.substring(7));
        return s;
    }

    function applySaveOptions() {
        root.app.setSaveOptions(saveCheck.checked, saveDirDialog.selectedFolder);
    }

    SplitView.preferredWidth: 300
    SplitView.minimumWidth: 200
    SplitView.maximumWidth: 350
    padding: 10
    // 侧边栏用面板色，与主区区分开
    background: Rectangle {
        color: root.theme.panel
        border.color: root.theme.border
        border.width: 1
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        SplitView {
            id: sidebarSplit
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Vertical

            // ---- 当前配置：弹性块之一，与保存块平分侧栏剩余高度 ----
            Rectangle {
                SplitView.fillWidth: true
                SplitView.fillHeight: true
                SplitView.preferredHeight: 250
                SplitView.minimumHeight: 100
                radius: 4
                clip: true
                color: root.theme.base
                border.color: root.theme.border
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 5

                    Label {
                        text: "当前配置："
                        font.bold: true
                        font.pixelSize: 13
                    }

                    // 滑动配置显示
                    Flickable {
                        id: configFlick
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        contentWidth: width
                        contentHeight: configColumn.height
                        boundsBehavior: Flickable.StopAtBounds
                        clip: true
                        interactive: contentHeight > height
                        ScrollBar.vertical: ScrollBar {}
                        Column {
                            id: configColumn
                            width: configFlick.width
                            spacing: 6

                            Repeater {
                                model: [
                                    {
                                        "k": "模型：",
                                        "v": root.app.config.modelPath
                                    },
                                    {
                                        "k": "类别：",
                                        "v": root.app.config.classNames.join(", ")
                                    },
                                    {
                                        "k": "置信度：",
                                        "v": root.app.config.confThreshold.toFixed(2)
                                    },
                                    {
                                        "k": "NMS：",
                                        "v": root.app.config.iouThreshold.toFixed(2)
                                    },
                                    {
                                        "k": "输入尺寸：",
                                        "v": root.app.config.inputWidth + "×" + root.app.config.inputHeight
                                    },
                                    {
                                        "k": "线程数：",
                                        "v": String(root.app.config.numThreads)
                                    },
                                    {
                                        "k": "设备号：",
                                        "v": String(root.app.config.deviceId)
                                    },
                                    {
                                        "k": "模型存在：",
                                        "v": root.app.config.modelExists ? "是" : "否"
                                    },
                                    {
                                        "k": "用户设置模式：",
                                        "v": root.app.config.preferredModeText()
                                    },
                                    {
                                        "k": "实际生效模式：",
                                        "v": root.app.config.effectiveModeText()
                                    }
                                ]

                                // 根用 Column(位置器)：高度随换行后的内容自适应
                                delegate: Column {
                                    required property var modelData
                                    width: parent.width
                                    spacing: 1

                                    Label {
                                        text: modelData.k
                                        opacity: 0.6
                                        font.pixelSize: 12
                                    }

                                    Label {
                                        width: parent.width
                                        text: (modelData.v && modelData.v.length > 0) ? modelData.v : "—"
                                        wrapMode: Text.Wrap
                                        font.pixelSize: 11
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // 类别-颜色图例
            Rectangle {
                radius: 4
                color: root.theme.base
                border.color: root.theme.border
                border.width: 1
                SplitView.fillWidth: true
                SplitView.preferredHeight: 200
                SplitView.minimumHeight: 100

                ColumnLayout {
                    spacing: 8
                    anchors.fill: parent
                    anchors.margins: 8

                    Label {
                        text: "颜色-类别（共%1个类别）：".arg(root.app.classNames.length)
                        font.bold: true
                        font.pixelSize: 13
                    }

                    Flickable {
                        id: clsClrFlick
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        interactive: contentHeight > height
                        ScrollBar.vertical: ScrollBar {}
                        contentWidth: width
                        contentHeight: clsClrGrid.implicitHeight

                        GridLayout {
                            id: clsClrGrid
                            width: clsClrFlick.width
                            columns: 2
                            rowSpacing: 3
                            columnSpacing: 5

                            Repeater {
                                model: root.app.clsClrInfo
                                delegate: Rectangle {
                                    required property var modelData
                                    opacity: 0.8
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 30
                                    radius: 15

                                    color: modelData.clrName()

                                    Label {
                                        anchors.centerIn: parent
                                        width: parent.width - 8
                                        horizontalAlignment: Text.AlignHCenter
                                        renderTypeQuality: Text.HighRenderTypeQuality
                                        font.bold: true
                                        elide: Text.ElideRight
                                        text: "%1（编号：%2）".arg(modelData.clsName()).arg(modelData.cls())
                                        font.pixelSize: 10
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // 结果输出
            Rectangle {
                radius: 4
                SplitView.fillWidth: true
                SplitView.fillHeight: true
                SplitView.preferredHeight: 100
                SplitView.minimumHeight: 50
                color: root.theme.base
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 2
                    CheckBox {
                        id: saveCheck
                        Layout.fillWidth: true
                        text: "保存推理结果：%1".arg(checked ? "已打开" : "已关闭")
                        font.bold: true
                        font.pointSize: 13
                        icon.color: "#000000"
                        enabled: !root.app.busy
                        onToggled: {
                            if (checked && saveDirDialog.selectedFolder.toString().length === 0) {
                                checked = false;
                                saveDirDialog.open();
                            } else {
                                root.applySaveOptions();
                            }
                        }
                    }

                    Frame {
                        Layout.fillHeight: true
                        Layout.fillWidth: true
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 3
                            Button {
                                Layout.fillWidth: true
                                text: "选择…"
                                enabled: !root.app.busy
                                onClicked: saveDirDialog.open()
                            }
                            Label {
                                id: saveDirLabel
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                elide: Text.ElideMiddle
                                font.pixelSize: 12
                                opacity: root.app.saveDir.toString().length > 0 ? 1.0 : 0.55
                                text: root.app.saveDir.toString().length > 0 ? root.localPathFromUrl(root.app.saveDir) : "未选择保存目录"
                                HoverHandler {
                                    id: saveDirHover
                                }
                                ToolTip.visible: saveDirHover.hovered
                                ToolTip.delay: 400
                                ToolTip.text: saveDirLabel.text
                            }
                        }
                    }
                }
            }

            // 状态栏
            Rectangle {
                SplitView.fillWidth: true
                SplitView.preferredHeight: 80
                SplitView.minimumHeight: 50
                color: root.theme.base
                border.color: root.theme.border
                border.width: 1
                radius: 4

                RowLayout {
                    anchors.fill: parent
                    spacing: 3

                    // 级别色条
                    Rectangle {
                        Layout.preferredWidth: 5
                        Layout.fillHeight: true
                        radius: 5
                        color: root.theme.levelColor(root.statusLevel)
                    }

                    Frame {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 2
                            spacing: 4
                            Label {
                                Layout.fillWidth: true
                                text: root.statusTime.length > 0 ? ("状态 · " + root.statusTime) : "状态"
                                font.pixelSize: 12
                            }
                            Label {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                text: root.statusText
                                wrapMode: Text.Wrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                                color: root.theme.levelColor(root.statusLevel)
                                font.pixelSize: 9
                            }
                        }
                    }
                }
            }

            // 开始检测(自动按文件类型选择帧源)
            Rectangle {
                SplitView.fillWidth: true
                SplitView.preferredHeight: 80
                SplitView.minimumHeight: 50
                SplitView.maximumHeight: 100
                color: root.theme.base
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 3
                    Button {
                        id: detectBtn
                        property bool isBusy: root.app.busy
                        text: isBusy ? "停止检测" : "开始检测"
                        Layout.fillWidth: true
                        highlighted: true
                        onClicked: isBusy ? root.app.cancelCurrentJob() : root.app.onDetectClicked()
                    }

                    // 一键示例检测
                    Button {
                        text: "示例检测"
                        Layout.fillWidth: true
                        enabled: !root.app.busy
                        onClicked: root.app.runSampleDetection()
                    }
                    Item {
                        Layout.bottomMargin: 5
                    }
                }
                Shortcuts {
                    targetItem: root
                    active: root.app.busy
                    stopButton: detectBtn
                }
            }
        }
    }

    // 结果保存目录选择
    FolderDialog {
        id: saveDirDialog
        title: "选择结果保存目录"
        onAccepted: {
            saveCheck.checked = true;		// 选完目录自动勾上保存
            root.applySaveOptions();
            Logger.info("[UI]: 结果将保存到该目录下的 detection_results/");
        }
    }

    // 状态栏订阅日志流：与日志列表同源
    Connections {
        target: Logger
        function onMessageLogged(level, levelName, time, message) {
            root.statusLevel = level;
            root.statusTime = time;
            root.statusText = message;
        }
    }
}
