pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia

// main显示主区
ColumnLayout {
    id: root

    property var theme: null
    property var app: null
    property var perfWindow: null

    //  界面状态 
    property string selectedFileName: ""		// 已选文件名(仅显示)
    property url selectedFileUrl: ""			// 已选文件 url(供视频预览)
    property bool videoMode: false				// 当前选择模式：false=图片, true=视频
    property bool logVisible: true				// 日志面板期望可见性

    signal configRequested()
    signal ffmpegDeviceRequested()
    signal hikCameraRequested()
    signal perfRequested()

    spacing: 0

    //  工具栏 
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
                enabled: !root.app.busy
                onClicked: fileMenu.open()
            }
            // 已选文件名
            Label {
                Layout.fillWidth: true
                Layout.minimumWidth: 120
                elide: Text.ElideMiddle
                text: root.selectedFileName.length > 0 ? "已选文件：" + root.selectedFileName : "未选择文件"
                opacity: root.selectedFileName.length > 0 ? 1.0 : 0.55
            }

            // 摄像头检测(先弹对话框选设备
            Button {
                id: deviceBtn
                text: "摄像头检测"
                enabled: !root.app.busy
                onClicked: root.ffmpegDeviceRequested()
            }

            // 海康工业相机检测
            Button {
                text: "海康相机检测"
                enabled: !root.app.busy && root.app.PluginExists("Hik")
                onClicked: root.hikCameraRequested()
            }

            // 批量检测
            Button {
                text: "批量检测"
                enabled: !root.app.busy
                onClicked: batchDialog.open()
            }

            // 修改配置(弹出模态配置对话框)
            Button {
                text: "修改检测配置"
                highlighted: true
                enabled: !root.app.busy
                onClicked: root.configRequested()
            }

            // 性能监控页(非模态窗口)
            Button {
                text: "性能监控"
                highlighted: true
                onClicked: {
                    root.logVisible = false; // 打开后关闭日志	
                    root.perfRequested();
                }
            }
            Item {
                Layout.rightMargin: 8
            }
        }

        // 文件选择菜单
        Menu {
            id: fileMenu
            parent: fileSelectBtn
            y: fileSelectBtn.height
            MenuItem {
                text: "选择图片…"
                onTriggered: {
                    root.videoMode = false;
                    fileDialog.open();
                }
            }
            MenuItem {
                text: "选择视频…"
                onTriggered: {
                    root.videoMode = true;
                    fileDialog.open();
                }
            }
        }
    }

    //  显示区 + 日志区 
    SplitView {
        id: splitView
        Layout.fillWidth: true
        Layout.fillHeight: true
        orientation: Qt.Vertical

        //  检测结果显示区 
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
                        text: (root.perfWindow && root.perfWindow.isSummary && root.perfWindow.hasData ? "汇总 · " : "")
                            + "整体帧率：%1".arg(root.perfWindow && root.perfWindow.displaySnapshot
                                ? Number(root.perfWindow.displaySnapshot.overall.overallFPS).toFixed(1) : "0.0")
                        font.bold: true
                        visible: root.perfWindow && root.perfWindow.perf ? root.perfWindow.perf.levelIndex > 0 : false
                        color: "#d8249e13"
                        wrapMode: Text.WordWrap
                    }
                    Label {
                        elide: Text.ElideNone
                        visible: root.perfWindow && root.perfWindow.perf ? root.perfWindow.perf.levelIndex > 0 : false
                        text: "端到端耗时：%1".arg(root.perfWindow && root.perfWindow.displaySnapshot
                            ? Number(root.perfWindow.displaySnapshot.overall.broadEndToEndMs).toFixed(1) + "ms" : "无效")
                        color: "#d8249e13"
                        font.bold: true
                        wrapMode: Text.WordWrap
                    }

                    // 进度条
                    ThemedProgressBar {
                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        value: root.app.progress
                        indeterminate: root.app.progressIndeterminate
                        visible: root.app.busy
                        theme: root.theme
                    }

                    //  结果回看 
                    Label {
                        text: root.app.resultInfo
                        opacity: 0.6
                        font.pixelSize: 12
                        visible: detectionImage.visible
                    }

                    Button {
                        text: "上一个"
                        enabled: root.app.resultCount > 0 && (!root.app.reviewing || root.app.resultIndex > 0)
                        onClicked: root.app.prevResult()
                        visible: detectionImage.visible
                    }

                    Button {
                        text: "下一个"
                        enabled: root.app.resultCount > 0 && (!root.app.reviewing || root.app.resultIndex < root.app.resultCount - 1)
                        onClicked: root.app.nextResult()
                        visible: detectionImage.visible
                    }

                    Button {
                        text: "回到实时"
                        visible: root.app.reviewing && root.app.busy
                        onClicked: root.app.followLatest()
                    }
                    Item {
                        Layout.rightMargin: 3
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: root.theme.base
                    border.color: root.theme.border
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
                        visible: root.app.frameRevision > 0 && !detectionOverlay.active
                        source: visible ? root.app.frameSource : ""
                    }

                    // 实时叠加层
                    DetectionOverlayItem {
                        id: detectionOverlay
                        anchors.fill: parent
                        anchors.margins: 4
                        readonly property bool active: frameReady && !root.app.reviewing
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
                        visible: !root.videoMode && root.app.frameRevision === 0 && !detectionOverlay.active
                    }

                    // 尚未开始检测时的视频预览
                    VideoOutput {
                        id: videoOutput
                        anchors.fill: parent
                        anchors.margins: 4
                        fillMode: VideoOutput.PreserveAspectFit
                        visible: root.videoMode && root.app.frameRevision === 0 && !detectionOverlay.active
                    }

                    // 占位提示
                    Label {
                        anchors.centerIn: parent
                        horizontalAlignment: Text.AlignHCenter
                        text: "尚无检测结果\n请选择图片 / 视频，或启动摄像头检测"
                        color: root.theme.muted
                        visible: root.selectedFileName.length === 0 && root.app.frameRevision === 0 && !detectionOverlay.active
                    }
                }
            }
        }

        //  日志
        LogView {
            id: logView
            theme: root.theme
            SplitView.preferredHeight: 190
            SplitView.minimumHeight: 90
            // 打开性能窗口关闭
            visible: root.logVisible && (!root.perfWindow || !root.perfWindow.visible)
            onCloseRequested: root.logVisible = false
        }
        Button {
            id: openLogBtn
            text: "打开日志"
            visible: !root.logVisible && (!root.perfWindow || !root.perfWindow.visible)
            onClicked: root.logVisible = true
        }
    }

    // 视频播放器
    MediaPlayer {
        id: mediaPlayer
        loops: MediaPlayer.Infinite
        source: root.selectedFileUrl
        videoOutput: videoOutput
        onErrorOccurred: function (error, errorString) {
            Logger.warning("[UI]: 视频播放失败: " + errorString);
        }
    }

    // 检测开始后暂停原始视频预览
    Connections {
        target: root.app
        function onBusyChanged() {
            if (root.app.busy && root.videoMode)
                mediaPlayer.pause();
        }
    }

    //  文件选择对话框
    MediaFileDialog {
        id: fileDialog
        category: root.videoMode ? "video" : "image"
        onAccepted: {
            root.selectedFileName = fileDialog.selectedFileName;
            root.app.setFilePath(fileDialog.selectedFile);			
            if (root.videoMode) {
                root.selectedFileUrl = fileDialog.selectedFile;
                mediaPlayer.play();
            } else {
                root.selectedFileUrl = "";			
                resultImage.source = fileDialog.selectedFile;
            }
            Logger.info("[UI]: 已选择" + (root.videoMode ? "视频" : "图片") + "：" + root.selectedFileName);
        }
        onRejected: Logger.info("[UI]: 已取消选择文件")
    }

    // 批量检测
    MediaFileDialog {
        id: batchDialog
        category: "av"
        multiSelect: true
        dialogTitle: "选择要批量检测的文件（可多选）"
        onAccepted: {
            if (batchDialog.selectedFiles.length === 0)
                return;
            Logger.info("[UI]: 提交批量检测，文件数：" + batchDialog.selectedFiles.length);
            root.app.submitBatch(batchDialog.selectedFiles);
        }
        onRejected: Logger.info("[UI]: 已取消批量选择")
    }
}
