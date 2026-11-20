pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

// ============================================================================
//  性能监控页(非模态窗口)
//  左侧：性能面板 —— 配置 | 总体计数 / 上下文信息 | 阶段指标(2×2 分区)
//  右侧：日志(LogView 复用)
//  分区壳(圆角 Rectangle + 左上角分区名)只在本文件使用，直接内联为组件。
// ============================================================================
Window {
    id: performanceView
    visible: false
    modality: Qt.NonModal
    title: "性能监控页"
    width: 1000
    height: 800
    minimumWidth: 500 
    minimumHeight: 400
    color: performanceView.theme.window

    property var theme: null		// 由外部注入的主题对象(Theme.qml)
    property var perf: null			// PerformanceApp(全局上下文)

    // 性能级别选项(与 PerformanceConfig::PerformanceLevel 枚举值一一对应)
    readonly property var levelOptions: ["关闭(Off)", "基础(Basic)", "详细(Detailed)", "完整(Full)"]

    // ------------------------------------------------------------------
    //  分区壳：圆角 Rectangle + 左上角分区名(本文件内复用，不导出)
    // ------------------------------------------------------------------
    component SectionBox: Rectangle {
        id: section

        property string sectionTitle: ""
        default property alias content: contentArea.data

        radius: 8
        color: performanceView.theme.panel
        border.color: performanceView.theme.border
        border.width: 1
        clip: true

        Label {
            id: titleLabel
            x: 14
            y: 6
            text: section.sectionTitle
            font.bold: true
            font.pixelSize: 13
            color: performanceView.theme.windowText
            z: 1
            background: Rectangle {
                radius: 4
                color: section.color	// 用底色盖住穿过标题的子元素边线
            }
        }

        Item {
            id: contentArea
            anchors.fill: parent
            anchors.topMargin: 26
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            anchors.bottomMargin: 10
        }
    }

    // ------------------------------------------------------------------
    //  横幅(复用主界面 banner 的视觉设计)：一条槽位，按级别换配色
    // ------------------------------------------------------------------
    readonly property bool bannerVisible: performanceView.perf && performanceView.perf.bannerText.length > 0
    // bannerLevel 数值与 PerformanceApp::BannerLevel 对应：0=None 1=Warning 2=Error
    readonly property bool bannerIsError: performanceView.perf && performanceView.perf.bannerLevel === 2

    RowLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10

        // ================= 左侧性能面板 =================
        ColumnLayout {
            Layout.preferredWidth: 560
            Layout.fillHeight: true
            Layout.maximumWidth: parent.width * 0.6
            spacing: 10

            // ---- 横幅 ----
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: performanceView.bannerVisible ? 34 : 0
                visible: performanceView.bannerVisible
                color: performanceView.bannerIsError ? (performanceView.theme.dark ? "#3d0f0f" : "#fde3e3") : (performanceView.theme.dark ? "#3d3208" : "#fff4d6")
                border.color: performanceView.bannerIsError ? performanceView.theme.logError : performanceView.theme.logWarning
                border.width: 1
                radius: 4

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 6
                    spacing: 8

                    Rectangle {
                        Layout.preferredWidth: 4
                        Layout.preferredHeight: 16
                        radius: 2
                        color: performanceView.bannerIsError ? performanceView.theme.logError : performanceView.theme.logWarning
                    }

                    Label {
                        Layout.fillWidth: true
                        text: performanceView.bannerVisible ? performanceView.perf.bannerText : ""
                        elide: Text.ElideMiddle
                        color: performanceView.bannerIsError ? (performanceView.theme.dark ? "#ffb0b0" : "#8a1a1a") : (performanceView.theme.dark ? "#ffd9a0" : "#7a4a00")
                        font.pixelSize: 12
                    }

                    Button {
                        text: "知道了"
                        flat: true
                        onClicked: if (performanceView.perf)
                            performanceView.perf.dismissBanner()
                    }
                }
            }

            // ---- 2×2 分区 ----
            GridLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                columns: 2
                columnSpacing: 10
                rowSpacing: 10

                // ============ 左上：配置 ============
                SectionBox {
                    sectionTitle: "配置"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 270

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 8
                        // 选择级别
                        ComboBox {
                            id: levelCombo
                            Layout.fillWidth: true
                            model: performanceView.levelOptions
                            currentIndex: performanceView.perf ? performanceView.perf.levelIndex : 1
                            onActivated: function (idx) {
                                if(!performanceView.perf) return;
                                // 校验是否为On/Off
                                const cur = performanceView.perf.levelIndex;
                                if((idx===0)!==(cur===0)){
                                    restartDialog.pendingIndex=idx;
                                    restartDialog.open();
                                    return; // 取消退出
                                }
                                // 不是on<->off就直接应用
                                performanceView.perf.levelIndex = idx;
                            }
                        }
                        // 设置上报间隔
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            Label {
                                text: "上报间隔"
                                font.pixelSize: 12
                            }
                            SpinBox {
                                id: intervalSpin
                                Layout.fillWidth: true
                                from: 50
                                to: 5000
                                stepSize: 50
                                editable: true
                                value: performanceView.perf ? performanceView.perf.reportInterval : 300
                                onValueModified: if (performanceView.perf)
                                    performanceView.perf.reportInterval = value
                                HoverHandler {
                                    id: intervalHover
                                }
                                ToolTip.visible: intervalHover.hovered
                                ToolTip.delay: 600
                                ToolTip.text: "性能快照的上报周期(毫秒)"
                            }
                            Label {
                                text: "ms"
                                font.pixelSize: 12
                                opacity: 0.6
                            }
                        }
                        // 级别说明
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            font.pixelSize: 11
                            opacity: 0.6
                            color: performanceView.theme.muted
                            text: "级别决定采集范围：Basic 仅端到端；\nDetailed 加 CPU 各阶段与下载；\nFull 再加 GPU 侧上传。\n开关监控需重启程序生效。"
                        }
                        // 运行状态
                        PerformanceRect {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            theme: performanceView.theme
                            label: "运行状态"
                            textOverride: performanceView.perf ? performanceView.perf.isRunning ? "正在运行检测" : "空闲" : "未初始化监控器"
                        }

                        Item {
                            Layout.fillHeight: true
                        }
                    }
                }

                // ============ 右上：总体计数 ============
                SectionBox {
                    sectionTitle: "总体计数"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 270

                    GridLayout {
                        anchors.fill: parent
                        columns: 2
                        columnSpacing: 8
                        rowSpacing: 8

                        PerformanceRect {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            theme: performanceView.theme
                            label: "整体帧率"
                            value: performanceView.perf ? performanceView.perf.overallFPS : 0
                            unit: "fps"
                            decimals: 1
                        }
                        PerformanceRect {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            theme: performanceView.theme
                            label: "狭义端到端"
                            value: performanceView.perf ? performanceView.perf.narrowEndToEndMs : 0
                            unit: "ms"
                            caption: "读帧 → 推理完成"
                        }
                        PerformanceRect {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            theme: performanceView.theme
                            label: "广义端到端"
                            value: performanceView.perf ? performanceView.perf.broadEndToEndMs : 0
                            unit: "ms"
                            caption: "读帧 → 渲染/保存完成"
                        }
                        PerformanceRect {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            theme: performanceView.theme
                            label: "统计窗口"
                            value: performanceView.perf ? performanceView.perf.windowDurationMs : 0
                            unit: "ms"
                            decimals: 0
                            caption: "第 " + (performanceView.perf ? performanceView.perf.snapshotIndex : 0) + " 号快照"
                        }
                    }
                }

                // ============ 左下：上下文信息 ============
                SectionBox {
                    sectionTitle: "上下文信息"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 270

                    GridLayout {
                        anchors.fill: parent
                        columns: 2
                        columnSpacing: 8
                        rowSpacing: 8

                        PerformanceRect {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            theme: performanceView.theme
                            label: "作业 ID"
                            value: performanceView.perf ? Number(performanceView.perf.jobId) : 0
                            decimals: 0
                            caption: performanceView.perf && performanceView.perf.jobId === 0 ? "空闲" : "运行中"
                        }
                        PerformanceRect {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            theme: performanceView.theme
                            label: "监控级别"
                            textOverride: performanceView.levelOptions[performanceView.perf ? performanceView.perf.levelIndex : 1]
                        }
                        PerformanceRect {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            theme: performanceView.theme
                            label: "用户设置模式"
                            textOverride: performanceView.perf ? performanceView.perf.preferredModeText : "—"
                        }
                        PerformanceRect {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            theme: performanceView.theme
                            label: "实际生效模式"
                            textOverride: performanceView.perf ? performanceView.perf.activeModeText : "—"
                        }
                    }
                }

                // ============ 右下：阶段指标 ============
                SectionBox {
                    sectionTitle: "阶段指标"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 270

                    // 监控关闭时给空态，而不是一排假 0
                    Label {
                        anchors.centerIn: parent
                        visible: !performanceView.perf || performanceView.perf.levelIndex === 0
                        text: "性能监控已关闭"
                        color: performanceView.theme.muted
                        font.pixelSize: 13
                    }

                    Label {
                        anchors.centerIn: parent
                        visible: performanceView.perf && performanceView.perf.levelIndex > 0 && performanceView.perf.stageStats.length === 0
                        text: "暂无阶段数据\n(启动检测后出现)"
                        horizontalAlignment: Text.AlignHCenter
                        color: performanceView.theme.muted
                        font.pixelSize: 13
                    }

                    // 阶段行数随监控级别变化
                    GridLayout {
                        anchors.fill: parent
                        visible: performanceView.perf && performanceView.perf.levelIndex > 0 && performanceView.perf.stageStats.length > 0
                        columns: 2
                        columnSpacing: 8
                        rowSpacing: 8

                        Repeater {
                            model: performanceView.perf ? performanceView.perf.stageStats.length : 0

                            PerformanceRect {
                                required property int index
                                readonly property var row: performanceView.perf ? performanceView.perf.stageStats[index] : null
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                theme: performanceView.theme
                                label: row ? row.name : ""
                                value: row ? row.averageMs : 0
                                unit: "ms"
                                caption: row ? ("最大 " + Number(row.maximumMs).toFixed(1) + " ms " + "\n平均 " + Number(row.averageMs).toFixed(1) + " ms\n共 " + row.sampleCount + " 次") : ""
                            }
                        }
                    }
                }
            }
        }

        // ================= 右侧日志 =================
        SectionBox {
            sectionTitle: "日志"
            Layout.fillWidth: true
            Layout.fillHeight: true

            LogView {
                anchors.fill: parent
                theme: performanceView.theme
            }
        }
    }
    // 提醒重启应用弹窗
    MessageDialog {
    id: restartDialog
    visible:false
    property bool restarting: false
    property int pendingIndex: -1
    title: "关闭/启动性能监控需要重启应用"
    text: "为了使更改生效，需要重启应用。"
    buttons: MessageDialog.Ok | MessageDialog.Cancel
    onAccepted: {
        if (restartDialog.restarting) return;
        restartDialog.restarting = true;
        performanceView.perf.levelIndex = restartDialog.pendingIndex; // 确定设置级别
        appHelper.restart();
     }
     onRejected:{
        levelCombo.currentIndex = performanceView.perf ? performanceView.perf.levelIndex : 0;
     }
    }
}