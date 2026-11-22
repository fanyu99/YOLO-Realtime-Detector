pragma ComponentBehavior: Bound

import QtQuick.Controls
import QtQuick
import QtQuick.Layouts
import QtQuick.Dialogs
import QtMultimedia

// ============================================================================
//  检测头 —— 主界面
//  顶部：标题+预览模式调节 + 主题显示
//  左侧：常驻配置栏，展示 DetectionApp.config
//  右侧：工具栏 + 检测结果显示区 + 日志区
// ============================================================================
Window {
    id: window
    visible: true
    width: 1000
    height: 800
    minimumWidth: 600
    minimumHeight: 400
    title: "检测头"
    color: theme.window

    // 主题(跟随系统) 与 后端门面
    Theme {
        id: theme
    }
    // 后端App(全局单例),负责将QML与C++桥接，提供配置、日志、检测等功能

    // ---- 界面状态 ----
    property string selectedFileName: ""		// 已选文件名(仅显示)
    property url selectedFileUrl: ""			// 已选文件 url(供图片 / 视频预览)
    property bool videoMode: false				// 当前选择模式：false=图片, true=视频
    property bool logVisible: true				// 日志面板期望可见性；打开 perfWindow / 关闭日志时置为 false

    // ---- 状态栏(与日志同源：订阅 Logger.messageLogged) ----
    property int statusLevel: 1					// 当前状态级别(默认 INFO)
    property string statusText: "就绪"			// 当前状态文本
    property string statusTime: ""				// 状态时间

    // 从 url 中取出文件名
    function fileNameFromUrl(u) {
        var s = u.toString();
        var i = s.lastIndexOf('/');
        return i >= 0 ? s.substring(i + 1) : s;
    }

    // 显示完整路径
    function localPathFromUrl(u) {
        var s = u.toString();
        if (s.startsWith("file:///"))
            s = decodeURIComponent(s.substring(8));
        else
        // "file:///" 后才是盘符路径
        if (s.startsWith("file://"))
            s = decodeURIComponent(s.substring(7));
        return s;
    }

    function applySaveOptions() {
        DetectionApp.setSaveOptions(saveCheck.checked, saveDirDialog.selectedFolder);
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ====================================================================
        //  顶部标题栏
        // ====================================================================
        ToolBar {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                Label {
                    text: "实时检测器"
                    font.bold: true
                    font.pixelSize: 18
                }
                Label {
                    text: "当前预览模式："
                    font.bold: true
                    font.pixelSize: 13
                }
                Label {
                    text: DetectionApp.preferredViewMode === "offline" ? "离线预览（%1）".arg(DetectionApp.activeViewMode) : "实时预览（%1）".arg(DetectionApp.activeViewMode)
                    font.bold: true
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }

                Label {
                    text: "选择预览模式"
                    font.bold: true
                    font.pixelSize: 13
                }
                ComboBox {
                    id: viewModeCombo
                    enabled: !DetectionApp.busy
                    implicitContentWidthPolicy: ComboBox.WidestText
                    model: ["离线预览（offline）", "实时预览（realtime）"]
                    currentIndex: DetectionApp.preferredViewMode === "offline" ? 0 : 1
                    onActivated: {
                        var modeStr = currentIndex === 0 ? "offline" : "realtime";
                        DetectionApp.setViewMode(modeStr);
                    }
                }
                Item {
                    Layout.fillWidth: true
                }
                Label {
                    Layout.rightMargin: 3
                    text: theme.dark ? "主题：深色（跟随系统）" : "主题：浅色（跟随系统）"
                    opacity: 0.75
                }
            }
        }

        // 警告横幅
        Rectangle {
            id: warningBanner
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 34 : 0
            visible: DetectionApp.bannerText.length > 0
            readonly property bool isSuccess: DetectionApp.bannerLevel === DetectionApp.BannerSuccess
            color: isSuccess ? (theme.dark ? "#0d3321" : "#e3f7ec") : (theme.dark ? "#3d3208" : "#fff4d6")
            border.color: isSuccess ? theme.logSuccess : theme.logWarning
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 6
                spacing: 8

                // 左侧色条：与侧边栏状态栏保持同一套视觉语言
                Rectangle {
                    Layout.preferredWidth: 4
                    Layout.preferredHeight: 16
                    radius: 2
                    color: warningBanner.isSuccess ? theme.logSuccess : theme.logWarning
                }

                Label {
                    Layout.fillWidth: true
                    text: DetectionApp.bannerText
                    elide: Text.ElideMiddle
                    color: warningBanner.isSuccess ? (theme.dark ? "#9fe1cb" : "#0f5132") : (theme.dark ? "#ffd9a0" : "#7a4a00")
                    font.pixelSize: 12
                }

                Button {
                    text: "知道了"
                    flat: true
                    onClicked: DetectionApp.dismissBanner()
                }
                Timer {
                    id: hideTimer
                    interval: 5000
                    running: visible && warningBanner.isSuccess
                    repeat: false
                    onTriggered: DetectionApp.dismissBanner()
                }
            }
        }

        // ====================================================================
        //  主体：左侧配置栏 + 右侧主区
        // ====================================================================
        SplitView {
            id: mainSplit
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal

            // ---------------- 左侧常驻配置栏 ----------------
            Frame {
                id: sidebar
                SplitView.preferredWidth: 300
                SplitView.minimumWidth: 200
                SplitView.maximumWidth: 350
                padding: 10
                // 侧边栏用面板色，与主区区分开
                background: Rectangle {
                    color: theme.panel
                    border.color: theme.border
                    border.width: 1
                }

                ColumnLayout {
                    id: sidebarColumn
                    anchors.fill: parent
                    spacing: 8

                    SplitView {
                        id: sidebarSplit
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        orientation: Qt.Vertical

                        // 当前配置：弹性块之一，与保存块平分侧栏剩余高度
                        Rectangle {
                            SplitView.fillWidth: true
                            SplitView.fillHeight: true
                            SplitView.preferredHeight: 250
                            SplitView.minimumHeight: 100
                            radius: 4
                            clip: true
                            color: theme.base
                            border.color: theme.border
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
                                                    "v": DetectionApp.config.modelPath
                                                },
                                                {
                                                    "k": "类别：",
                                                    "v": DetectionApp.config.classNames.join(", ")
                                                },
                                                {
                                                    "k": "置信度：",
                                                    "v": DetectionApp.config.confThreshold.toFixed(2)
                                                },
                                                {
                                                    "k": "NMS：",
                                                    "v": DetectionApp.config.iouThreshold.toFixed(2)
                                                },
                                                {
                                                    "k": "输入尺寸：",
                                                    "v": DetectionApp.config.inputWidth + "×" + DetectionApp.config.inputHeight
                                                },
                                                {
                                                    "k": "线程数：",
                                                    "v": String(DetectionApp.config.numThreads)
                                                },
                                                {
                                                    "k": "设备号：",
                                                    "v": String(DetectionApp.config.deviceId)
                                                },
                                                {
                                                    "k": "模型存在：",
                                                    "v": DetectionApp.config.modelExists ? "是" : "否"
                                                },
                                                {
                                                    "k": "用户设置模式：",
                                                    "v": DetectionApp.config.preferredModeText()
                                                },
                                                {
                                                    "k": "实际生效模式：",
                                                    "v": DetectionApp.config.effectiveModeText()
                                                }
                                            ]

                                            // 根用 Column(位置器)：高度随换行后的内容自适应，
                                            // 之前的空 Rectangle + anchors.fill 行高实际为 0
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
                            id: class_colorRect
                            radius: 4
                            color: theme.base
                            border.color: theme.border
                            border.width: 1
                            SplitView.fillWidth: true
                            SplitView.preferredHeight: 200
                            SplitView.minimumHeight: 100

                            ColumnLayout {
                                spacing: 8
                                anchors.fill: parent
                                anchors.margins: 8

                                Label {
                                    text: "颜色-类别（共%1个类别）：".arg(DetectionApp.classNames.length)
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
                                            model: DetectionApp.clsClrInfo
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
                            color: theme.base
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
                                    enabled: !DetectionApp.busy
                                    onToggled: {
                                        if (checked && saveDirDialog.selectedFolder.toString().length === 0) {
                                            checked = false;
                                            saveDirDialog.open();
                                        } else {
                                            window.applySaveOptions();
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
                                            enabled: !DetectionApp.busy
                                            onClicked: saveDirDialog.open()
                                        }
                                        Label {
                                            id: saveDirLabel
                                            Layout.fillWidth: true
                                            Layout.fillHeight: true
                                            elide: Text.ElideMiddle
                                            font.pixelSize: 12
                                            opacity: DetectionApp.saveDir.toString().length > 0 ? 1.0 : 0.55
                                            text: DetectionApp.saveDir.toString().length > 0 ? window.localPathFromUrl(DetectionApp.saveDir) : "未选择保存目录"
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
                            color: theme.base
                            border.color: theme.border
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
                                    color: theme.levelColor(window.statusLevel)
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
                                            text: window.statusTime.length > 0 ? ("状态 · " + window.statusTime) : "状态"
                                            font.pixelSize: 12
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            Layout.fillHeight: true
                                            text: window.statusText
                                            wrapMode: Text.Wrap
                                            maximumLineCount: 2
                                            elide: Text.ElideRight
                                            color: theme.levelColor(window.statusLevel)
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
                            color: theme.base
                            ColumnLayout {
                                anchors.fill: parent
                                spacing: 3
                                Button {
                                    id: detectBtn
                                    property bool isBusy: DetectionApp.busy
                                    text: isBusy ? "停止检测" : "开始检测"
                                    Layout.fillWidth: true
                                    highlighted: true
                                    onClicked: isBusy ? DetectionApp.cancelCurrentJob() : DetectionApp.onDetectClicked()
                                }

                                // 一键示例检测
                                Button {
                                    text: "示例检测"
                                    Layout.fillWidth: true
                                    enabled: !DetectionApp.busy
                                    onClicked: DetectionApp.runSampleDetection()
                                }
                                Item {
                                    Layout.bottomMargin: 5
                                }
                            }
                            Shortcut {
                                sequence: "Esc"
                                enabled: detectBtn.isBusy
                                autoRepeat: false
                                onActivatedAmbiguously: Logger.warning("出现退出冲突！")
                                onActivated: function () {
                                    if (detectBtn.isBusy)
                                        DetectionApp.cancelCurrentJob();
                                }
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                SplitView.fillWidth: true
                spacing: 0

                // ---------- 工具栏 ----------
                ToolBar {
                    Layout.fillWidth: true
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 8

                        // 文件选择按钮(弹出菜单选择图片 / 视频)
                        Button {
                            id: fileSelectBtn
                            text: "选择文件  ▾"
                            enabled: !DetectionApp.busy
                            onClicked: fileMenu.open()
                        }
                        // 已选文件名
                        Label {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 120
                            elide: Text.ElideMiddle
                            text: window.selectedFileName.length > 0 ? "已选文件：" + window.selectedFileName : "未选择文件"
                            opacity: window.selectedFileName.length > 0 ? 1.0 : 0.55
                        }

                        // 摄像头检测(先弹对话框选设备；走 ffmpeg dshow/avfoundation/v4l2 采集)
                        Button {
                            id: deviceBtn
                            text: "摄像头检测"
                            enabled: !DetectionApp.busy
                            onClicked: ffmpegDeviceDialog.open()
                        }

                        // 海康工业相机检测(先弹对话框选设备；走 MVS SDK，与上面的 USB 编号不是一回事)
                        Button {
                            text: "海康相机检测"
                            enabled: !DetectionApp.busy
                            onClicked: hikCameraDialog.open()
                        }

                        // 批量检测(多选图片/视频，一个作业顺序跑完再汇总)
                        Button {
                            text: "批量检测"
                            enabled: !DetectionApp.busy
                            onClicked: batchDialog.open()
                        }

                        // 修改配置(弹出模态配置对话框)
                        Button {
                            text: "修改检测配置"
                            highlighted: true
                            enabled: !DetectionApp.busy
                            onClicked: configDialog.openWith(DetectionApp.config, DetectionApp.modelPath)
                        }

                        // 性能监控页(非模态窗口)
                        Button {
                            text: "性能监控"
                            highlighted: true
                            onClicked: {
                                window.logVisible = false;	// 打开性能页即收起日志，关闭后由"打开日志"按钮恢复
                                perfWindow.show();
                                perfWindow.raise();
                                perfWindow.requestActivate();
                            }
                        }
                        Item {
                            Layout.rightMargin: 8
                        }
                    }

                    // 文件选择菜单(挂到按钮下方弹出)
                    Menu {
                        id: fileMenu
                        parent: fileSelectBtn
                        y: fileSelectBtn.height
                        MenuItem {
                            text: "选择图片…"
                            onTriggered: {
                                window.videoMode = false;
                                fileDialog.open();
                            }
                        }
                        MenuItem {
                            text: "选择视频…"
                            onTriggered: {
                                window.videoMode = true;
                                fileDialog.open();
                            }
                        }
                    }
                }

                // ---------- 显示区 + 日志区 ----------
                SplitView {
                    id: splitView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    orientation: Qt.Vertical

                    // ---- 检测结果显示区 ----
                    Frame {
                        SplitView.fillHeight: true
                        SplitView.minimumHeight: 220
                        padding: 8

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 6

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                Item {
                                    Layout.leftMargin: 3
                                }

                                Label {
                                    text: "检测结果显示"
                                    font.bold: true
                                    font.pixelSize: 15
                                }

                                Label {
                                    elide: Text.ElideNone
                                    text: "整体帧率：%1".arg(perfWindow.perf ? perfWindow.perf.overallFPS.toFixed(1) : 0.0)
                                    font.bold: true
                                    visible: perfWindow.perf ? perfWindow.perf.levelIndex > 0 : false
                                    color: "#d8249e13"
                                    wrapMode: Text.WordWrap
                                }
                                Label {
                                    elide: Text.ElideNone
                                    visible: perfWindow.perf ? perfWindow.perf.levelIndex > 0 : false
                                    text: "端到端耗时：%1".arg(perfWindow.perf ? perfWindow.perf.broadEndToEndMs.toFixed(1) + "ms" : "无效")
                                    color: "#d8249e13"
                                    font.bold: true
                                    wrapMode: Text.WordWrap
                                }

                                // 进度：摄像头这类总量未知的走忙碌态

                                ProgressBar {
                                    id: progressBar
                                    Layout.fillWidth: true
                                    Layout.leftMargin: 12
                                    from: 0
                                    to: 1
                                    value: DetectionApp.progress
                                    indeterminate: DetectionApp.progressIndeterminate
                                    visible: DetectionApp.busy
                                    background: Rectangle {
                                        implicitWidth: 200
                                        implicitHeight: 24
                                        radius: 4
                                        border.color: theme.border
                                        color: "#e0e0e0"
                                    }
                                    contentItem: Item {
                                        Rectangle {
                                            width: progressBar.visualPosition * parent.width
                                            height: parent.height
                                            radius: 8
                                            gradient: Gradient {
                                                GradientStop {
                                                    position: 0.0

                                                    color: {
                                                        var v = progressBar.value * 100;
                                                        if (v < 20)
                                                            return "#FF0000";
                                                        else if (v < 40)
                                                            return "#FF00AA";
                                                        else if (v < 60)
                                                            return "#0055FF";
                                                        else if (v < 80)
                                                            return "#00DD99";
                                                        else
                                                            return "#00E600";
                                                    }
                                                }
                                                GradientStop {
                                                    position: 1.0
                                                    color: "#43a047"
                                                }
                                            }
                                        }
                                        Text {
                                            anchors.centerIn: parent
                                            text: Math.round(progressBar.value * 100) + "%"
                                            font.pixelSize: 13
                                            font.bold: true
                                            font.family: "Segoe UI, sans-serif"
                                            style: Text.Sunken
                                            styleColor: "#40ffffff"
                                        }
                                    }
                                }

                                // ---- 结果回看 ----
                                Label {
                                    text: DetectionApp.resultInfo
                                    opacity: 0.6
                                    font.pixelSize: 12
                                    visible: detectionImage.visible
                                }

                                Button {
                                    text: "上一个"
                                    enabled: DetectionApp.resultCount > 0 && (!DetectionApp.reviewing || DetectionApp.resultIndex > 0)
                                    onClicked: DetectionApp.prevResult()
                                    visible: detectionImage.visible
                                }

                                Button {
                                    text: "下一个"
                                    enabled: DetectionApp.resultCount > 0 && (!DetectionApp.reviewing || DetectionApp.resultIndex < DetectionApp.resultCount - 1)
                                    onClicked: DetectionApp.nextResult()
                                    visible: detectionImage.visible
                                }

                                Button {
                                    text: "回到实时"
                                    visible: DetectionApp.reviewing && DetectionApp.busy
                                    onClicked: DetectionApp.followLatest()
                                }
                                Item {
                                    Layout.rightMargin: 3
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                color: theme.base
                                border.color: theme.border
                                border.width: 1
                                radius: 4
                                clip: true

                                // 检测结果画面
                                Image {
                                    id: detectionImage
                                    anchors.fill: parent
                                    anchors.margins: 4
                                    fillMode: Image.PreserveAspectFit
                                    cache: false
                                    asynchronous: false
                                    visible: DetectionApp.frameRevision > 0 && !detectionOverlay.active
                                    source: visible ? DetectionApp.frameSource : ""
                                }

                                // 实时叠加层
                                DetectionOverlayItem {
                                    id: detectionOverlay
                                    anchors.fill: parent
                                    anchors.margins: 4
                                    readonly property bool active: frameReady && !DetectionApp.reviewing
                                    visible: active
                                }

                                // 尚未开始检测时的图片预览
                                Image {
                                    id: resultImage
                                    anchors.fill: parent
                                    anchors.margins: 4
                                    fillMode: Image.PreserveAspectFit
                                    cache: false
                                    asynchronous: true
                                    visible: !window.videoMode && DetectionApp.frameRevision === 0 && !detectionOverlay.active
                                }

                                // 尚未开始检测时的视频预览
                                VideoOutput {
                                    id: videoOutput
                                    anchors.fill: parent
                                    anchors.margins: 4
                                    fillMode: VideoOutput.PreserveAspectFit
                                    visible: window.videoMode && DetectionApp.frameRevision === 0 && !detectionOverlay.active
                                }

                                // 占位提示
                                Label {
                                    anchors.centerIn: parent
                                    horizontalAlignment: Text.AlignHCenter
                                    text: "尚无检测结果\n请选择图片 / 视频，或启动摄像头检测"
                                    color: theme.muted
                                    visible: window.selectedFileName.length === 0 && DetectionApp.frameRevision === 0 && !detectionOverlay.active
                                }
                            }
                        }
                    }

                    // ---- 日志区 ----
                    LogView {
                        id: logView
                        theme: theme
                        SplitView.preferredHeight: 190
                        SplitView.minimumHeight: 90
                        // perfWindow 显示时强制隐藏；打开 perfWindow 时 logVisible 已被置为 false，
                        // 故关闭 perfWindow 后日志保持关闭，由"打开日志"按钮恢复
                        visible: window.logVisible && !perfWindow.visible
                        onCloseRequested: window.logVisible = false
                    }
                    Button {
                        id: openLogBtn
                        text: "打开日志"
                        visible: !window.logVisible && !perfWindow.visible
                        onClicked: window.logVisible = true
                    }
                }
            }
        }
    }

    // ---------------- 右侧主区 ----------------

    // ========================================================================
    //  视频播放器：只用于"检测之前"预览所选视频。
    //  检测一旦开始，画面改由检测帧提供(detectionImage)，这里主动暂停，
    //  避免两路视频同时解码。
    // ========================================================================
    MediaPlayer {
        id: mediaPlayer
        loops: MediaPlayer.Infinite
        source: window.selectedFileUrl
        videoOutput: videoOutput
        onErrorOccurred: function (error, errorString) {
            Logger.warning("[UI]: 视频播放失败: " + errorString);
        }
    }

    // 检测开始后暂停原始视频预览
    Connections {
        target: DetectionApp
        function onBusyChanged() {
            if (DetectionApp.busy && window.videoMode)
                mediaPlayer.pause();
        }
    }

    // ========================================================================
    //  文件选择对话框
    // ========================================================================
    FileDialog {
        id: fileDialog
        title: window.videoMode ? "选择视频文件" : "选择图片文件"
        fileMode: FileDialog.OpenFile
        nameFilters: window.videoMode ? ["视频文件 (*.mp4 *.avi *.mkv *.mov *.wmv)", "所有文件 (*)"] : ["图片文件 (*.jpg *.jpeg *.png *.bmp *.webp)", "所有文件 (*)"]
        onAccepted: {
            window.selectedFileName = window.fileNameFromUrl(selectedFile);
            DetectionApp.setFilePath(selectedFile);			// 把文件路径交给后端
            if (window.videoMode) {
                window.selectedFileUrl = selectedFile;
                mediaPlayer.play();
            } else {
                window.selectedFileUrl = "";			// 切回图片时停掉视频
                resultImage.source = selectedFile;
            }
            Logger.info("[UI]: 已选择" + (window.videoMode ? "视频" : "图片") + "：" + window.selectedFileName);
        }
        onRejected: Logger.info("[UI]: 已取消选择文件")
    }

    // 批量检测：多选图片/视频，交给一个作业顺序跑完再汇总。
    // 传 url 列表给后端，路径转换在 C++ 侧用 toLocalFile() 做。
    FileDialog {
        id: batchDialog
        title: "选择要批量检测的文件（可多选）"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["图片与视频 (*.jpg *.jpeg *.png *.bmp *.webp *.mp4 *.avi *.mov *.mkv *.wmv)", "所有文件 (*)"]
        onAccepted: {
            if (selectedFiles.length === 0)
                return;
            Logger.info("[UI]: 提交批量检测，文件数：" + selectedFiles.length);
            DetectionApp.submitBatch(selectedFiles);
        }
        onRejected: Logger.info("[UI]: 已取消批量选择")
    }

    // 结果保存目录选择
    FolderDialog {
        id: saveDirDialog
        title: "选择结果保存目录"
        onAccepted: {
            saveCheck.checked = true;		// 选完目录自动勾上保存
            window.applySaveOptions();
            Logger.info("[UI]: 结果将保存到该目录下的 detection_results/");
        }
    }

    // 两个模态对话框
    ConfigDialog {
        id: configDialog
        theme: theme
        app: DetectionApp
    }
    HikCameraDialog {
        id: hikCameraDialog
        theme: theme
        app: DetectionApp
    }
    FfmpegDeviceDialog {
        id: ffmpegDeviceDialog
        theme: theme
        app: DetectionApp
    }

    // 性能监控页(非模态)
    PerformanceView {
        id: perfWindow
        theme: theme
        height: window.height * 0.9
        width: window.width * 0.9
        minimumHeight: window.minimumHeight
        minimumWidth: window.minimumWidth
        perf: perfApp
    }

    // 状态栏订阅日志流：与日志列表同源，
    // 选择文件 / 配置更新 / 错误 / 检测完成等都会自动刷新状态栏
    Connections {
        target: Logger
        function onMessageLogged(level, levelName, time, message) {
            window.statusLevel = level;
            window.statusTime = time;
            window.statusText = message;
        }
    }

    Component.onCompleted: {
        const warns = appConfig ? appConfig.warnings : [];
        for (let i = 0; i < warns.length; ++i)
            Logger.warning(warns[i]);
        Logger.success("[UI]: 界面已就绪，等待选择图片 / 视频");
    }
}
