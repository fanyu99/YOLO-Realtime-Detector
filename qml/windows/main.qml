import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 主界面
//  顶部：标题+预览模式调节 + 主题显示
//  左侧：DisplaySidebar（配置栏 / 状态 / 检测控制）
//  右侧：DisplayMain（工具栏 / 显示区 / 日志区）
Window {
    id: window
    visible: true
    width: 1200
    height: 900
    minimumWidth: 600
    minimumHeight: 450
    title: "检测头"
    color: appTheme.window

    // 主题
    AppTheme {
        id: appTheme
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        //  顶部标题栏
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
                    text: appTheme.dark ? "主题：深色（跟随系统）" : "主题：浅色（跟随系统）"
                    opacity: 0.75
                }
            }
        }

        // 警告横幅
        StatusBanner {
            Layout.fillWidth: true
            theme: appTheme
            visible: DetectionApp.bannerText.length > 0
            text: DetectionApp.bannerText
            level: DetectionApp.bannerLevel === DetectionApp.BannerSuccess ? "success"
                : DetectionApp.bannerLevel === DetectionApp.BannerWarning ? "warning"
                : "none"
            onDismissed: DetectionApp.dismissBanner()
        }
        // 主体分区
        // 左边 配置栏
        // 右边 显示区+日志
        SplitView {
            id: mainSplit
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal
            // 侧边栏
            DisplaySidebar {
                theme: appTheme
                app: DetectionApp
            }
            // 展示主区
            DisplayMain {
                SplitView.fillWidth: true
                theme: appTheme
                app: DetectionApp
                perfWindow: perfView
                onConfigRequested: configDialog.openWith(DetectionApp.config, DetectionApp.modelPath)
                onFfmpegDeviceRequested: ffmpegDeviceDialog.open()
                onHikCameraRequested: hikCameraDialog.open()
                onPerfRequested: {
                    perfView.show();
                    perfView.raise();
                    perfView.requestActivate();
                }
            }
        }
    }

    // 配置
    ConfigDialog {
        id: configDialog
        theme: appTheme
        app: DetectionApp
        modal: true 
    }
    // 海康摄像头
    HikCameraDialog {
        id: hikCameraDialog
        theme: appTheme
        app: DetectionApp
    }
    // 设备
    FfmpegDeviceDialog {
        id: ffmpegDeviceDialog
        theme: appTheme
        app: DetectionApp
    }

    // 性能监控页
    PerformanceView {
        id: perfView
        theme: appTheme
        height: window.height * 0.9
        width: window.width * 0.9
        minimumHeight: window.minimumHeight
        minimumWidth: window.minimumWidth
        perf: perfApp
    }
    // 完成时发日志
    Component.onCompleted: {
        const warns = appConfig ? appConfig.warnings : [];
        for (let i = 0; i < warns.length; ++i)
            Logger.warning(warns[i]);
        Logger.success("[UI]: 界面已就绪，等待选择图片 / 视频");
    }
}
