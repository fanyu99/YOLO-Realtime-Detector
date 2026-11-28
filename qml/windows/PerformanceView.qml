pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import PerformanceMemoryMonitor
// 性能窗口
//  上层：横幅
//  中层：配置+总体+上下文   阶段计时  内存占用
//  下层：日志
Window {
    id: performanceView
    visible: false
    modality: Qt.NonModal
    title: "性能监控页"
    width: 1200
    height: 820
    minimumWidth: 960
    minimumHeight: 620
    color: performanceView.theme.window

    property var theme: null		// 由外部注入的主题对象(AppTheme.qml)
    property var perf: null			// PerformanceApp
    property bool logVisible: true

    //  展示快照：停止时显示运行的汇总
    readonly property bool perfEnabled: !!performanceView.perf && performanceView.perf.levelIndex > 0
    readonly property bool isSummary: performanceView.perf ? !performanceView.perf.isRunning : false
    readonly property var displaySnapshot: performanceView.perf
        ? (performanceView.isSummary ? performanceView.perf.overallSnapshot : performanceView.perf.snapshot)
        : null
    readonly property var snapOverall: performanceView.displaySnapshot ? performanceView.displaySnapshot.overall : null
    readonly property var snapContext: performanceView.displaySnapshot ? performanceView.displaySnapshot.context : null
    readonly property var snapStages: performanceView.displaySnapshot ? performanceView.displaySnapshot.stages : []
    readonly property bool hasData: performanceView.snapContext ? performanceView.snapContext.windowDurationMs > 0 : false

    // 内存面板：插件可用且级别 >= Detailed 才有采样数据
    readonly property bool memoryPanelVisible: performanceView.perfEnabled
        && performanceView.perf
        && performanceView.perf.memoryMonitorAvailable
        && performanceView.perf.levelIndex >= 3

    // 性能级别选项
    readonly property var levelOptions: ["关闭(Off)", "最小(Minimum)", "基础(Basic)", "详细(Detailed)", "完整(Full)"]

    // 性能阶段计时说明
    readonly property string stageInfoHtml: `
        <table border="1" cellspacing="0" cellpadding="6" width="100%">
            <tr><th>阶段名</th><th>耗时说明</th></tr>
            <tr><td>预热</td><td>从检测器初始化预热配置到预热完成返回的耗时</td></tr>
            <tr><td>读帧耗时</td><td>从调用获取帧句柄到接收到帧句柄的耗时</td></tr>
            <tr><td>上传耗时</td><td>从创建图片 CPU 句柄到上传到显存句柄可用的耗时</td></tr>
            <tr><td>预处理耗时</td><td>从预处理器开始到预处理完成的耗时</td></tr>
            <tr><td>预处理 kernel 耗时</td><td>GPU 模式下 CUDA kernel 侧的计时</td></tr>
            <tr><td>推理耗时</td><td>从检测器开始检测到成功返回检测框结果的耗时</td></tr>
            <tr><td>后处理耗时</td><td>从后处理器开始到后处理完成的耗时</td></tr>
            <tr><td>下载耗时</td><td>从句柄下载/拷贝到 CPU 内存中的耗时</td></tr>
            <tr><td>渲染耗时</td><td>从开始渲染到完成为止的耗时</td></tr>
            <tr><td>转换 QImage 耗时</td><td>将图像数据转换为 QImage 的耗时</td></tr>
            <tr><td>kernel 转换耗时</td><td>实时预览模式下，互操作的 kernel 耗时</td></tr>
            <tr><td>显示准备</td><td>从开始投递图像快照到完成打包并投送图像快照到邮箱为止的耗时</td></tr>
            <tr><td>显示等待</td><td>从快照投递到邮箱到渲染线程取走快照准备渲染为止的耗时</td></tr>
            <tr><td>呈现延迟</td><td>从读帧开始到帧真正进入窗口显示为止的耗时</td></tr>
            <tr><td>丢弃帧</td><td>非耗时项，邮箱丢帧的数量</td></tr>
            <tr><td>保存耗时</td><td>从保存开始到写入到文件完成为止的耗时</td></tr>
            <tr><td>狭义端到端</td><td>从读帧开始到推理流程完成的耗时</td></tr>
            <tr><td>广义端到端</td><td>从读帧开始到完成渲染和保存结果完成为止的耗时</td></tr>
        </table>`
    // 组件：分区框
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

    //  横幅：级别映射成语义字符串后交给共享组件
    readonly property bool bannerVisible: performanceView.perf && performanceView.perf.bannerText.length > 0
    readonly property string bannerLevelText: !performanceView.perf
        ? "none"
        : performanceView.perf.bannerLevel === PerformanceApp.BannerSuccess
            ? "success"
            : performanceView.perf.bannerLevel === PerformanceApp.BannerError
                ? "error"
                : performanceView.perf.bannerLevel === PerformanceApp.BannerWarning ? "warning" : "none"
    // 主体
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10

        //  横幅
        StatusBanner {
            Layout.fillWidth: true
            visible: performanceView.bannerVisible
            theme: performanceView.theme
            text: performanceView.perf ? performanceView.perf.bannerText : ""
            level: performanceView.bannerLevelText
            autoDismissMs: performanceView.visible ? 5000 : 0
            onDismissed: if (performanceView.perf)
                performanceView.perf.dismissBanner()
        }

        //  中层 + 日志
        SplitView {
            id: mainSplit
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Vertical

            RowLayout {
                id: middleRow
                SplitView.fillWidth: true
                SplitView.fillHeight: true
                SplitView.minimumHeight: 180
                spacing: 10

                //  左列：配置 + 总体计数 + 上下文
                SectionBox {
                    sectionTitle: "配置 · 总体计数 · 上下文"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 320
                    Layout.minimumWidth: 260

                    Flickable {
                        id: leftFlick
                        anchors.fill: parent
                        contentWidth: width
                        contentHeight: leftColumn.implicitHeight
                        boundsBehavior: Flickable.StopAtBounds
                        clip: true
                        interactive: contentHeight > height
                        ScrollBar.vertical: ScrollBar {}

                        ColumnLayout {
                            id: leftColumn
                            width: leftFlick.width
                            spacing: 8

                            // 选择级别
                            ComboBox {
                                id: levelCombo
                                Layout.fillWidth: true
                                model: performanceView.levelOptions
                                currentIndex: performanceView.perf ? performanceView.perf.levelIndex : 1
                                onActivated: function (idx) {
                                    if (!performanceView.perf)
                                        return;
                                    // 校验是否为On/Off
                                    const cur = performanceView.perf.levelIndex;
                                    if ((idx === 0) !== (cur === 0)) {
                                        restartDialog.pendingIndex = idx;
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
                                    text: "性能上报间隔"
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
                            // 运行状态
                            PerformanceRect {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 90
                                theme: performanceView.theme
                                label: "运行状态"
                                textOverride: performanceView.perf
                                    ? (performanceView.perf.isRunning
                                        ? "正在运行检测"
                                        : (performanceView.hasData ? "空闲（显示汇总）" : "空闲"))
                                    : "未初始化监控器"
                            }

                            // 查看性能计时详细信息
                            Button {
                                id: infoBtn
                                Layout.fillWidth: true
                                text: perfInfoPopup.opened ? "关闭计时说明" : "查看计时说明"
                                onClicked: {
                                    if (perfInfoPopup.opened)
                                        perfInfoPopup.close();
                                    else
                                        perfInfoPopup.open();
                                }
                            }
                            Button {
                                id: logBtn
                                Layout.fillWidth: true
                                text: performanceView.logVisible ? "关闭日志" : "打开日志"
                                onClicked: performanceView.logVisible = !performanceView.logVisible
                            }

                            // ---- 总体计数 ----
                            Label {
                                Layout.fillWidth: true
                                Layout.topMargin: 4
                                text: performanceView.isSummary ? "总体计数 · 会话汇总" : "总体计数 · 实时窗口"
                                font.bold: true
                                font.pixelSize: 12
                                color: performanceView.theme.windowText
                            }
                            GridLayout {
                                Layout.fillWidth: true
                                columns: 2
                                columnSpacing: 8
                                rowSpacing: 8

                                PerformanceRect {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 86
                                    theme: performanceView.theme
                                    label: "整体帧率"
                                    value: performanceView.snapOverall ? performanceView.snapOverall.overallFPS : 0
                                    unit: "fps"
                                    decimals: 1
                                }
                                PerformanceRect {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 86
                                    theme: performanceView.theme
                                    label: "狭义端到端"
                                    value: performanceView.snapOverall ? performanceView.snapOverall.narrowEndToEndMs : 0
                                    unit: "ms"
                                    caption: "读帧 → 推理完成"
                                }
                                PerformanceRect {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 86
                                    theme: performanceView.theme
                                    label: "广义端到端"
                                    value: performanceView.snapOverall ? performanceView.snapOverall.broadEndToEndMs : 0
                                    unit: "ms"
                                    caption: "读帧 → 渲染/保存完成"
                                }
                                PerformanceRect {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 86
                                    theme: performanceView.theme
                                    label: performanceView.isSummary ? "有效统计时长" : "统计窗口"
                                    value: performanceView.isSummary
                                        ? (performanceView.snapOverall ? performanceView.snapOverall.validWindowDurationMs : 0)
                                        : (performanceView.snapContext ? performanceView.snapContext.windowDurationMs : 0)
                                    unit: "ms"
                                    decimals: 0
                                    caption: performanceView.isSummary
                                        ? (performanceView.snapContext
                                            ? "总时长 " + Number(performanceView.snapContext.windowDurationMs).toFixed(0) + " ms"
                                            : "")
                                        : (performanceView.snapContext ? "第 " + performanceView.snapContext.snapshotIndex + " 号快照" : "")
                                }
                                PerformanceRect {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 86
                                    theme: performanceView.theme
                                    label: "检测器预热总耗时"
                                    value: performanceView.snapOverall ? performanceView.snapOverall.warmupMs : 0
                                    unit: "ms"
                                    caption: "共 " + (performanceView.snapOverall ? performanceView.snapOverall.warmupIterations : 0) + " 轮"
                                }
                                PerformanceRect {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 86
                                    theme: performanceView.theme
                                    label: "记录帧数"
                                    value: performanceView.snapOverall ? performanceView.snapOverall.recordedFrames : 0
                                    decimals: 0
                                    caption: performanceView.snapOverall
                                        ? ("成功 " + performanceView.snapOverall.successfulFrames
                                            + " / 失败 " + performanceView.snapOverall.failedFrames)
                                        : ""
                                }
                            }

                            // ---- 上下文信息 ----
                            Label {
                                Layout.fillWidth: true
                                Layout.topMargin: 4
                                text: "上下文信息"
                                font.bold: true
                                font.pixelSize: 12
                                color: performanceView.theme.windowText
                            }
                            GridLayout {
                                Layout.fillWidth: true
                                columns: 2
                                columnSpacing: 8
                                rowSpacing: 8

                                PerformanceRect {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 78
                                    theme: performanceView.theme
                                    label: "作业 ID"
                                    value: performanceView.perf ? Number(performanceView.perf.jobId) : 0
                                    decimals: 0
                                    caption: performanceView.perf && performanceView.perf.jobId === 0 ? "空闲" : "运行中"
                                }
                                PerformanceRect {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 78
                                    theme: performanceView.theme
                                    label: "监控级别"
                                    textOverride: performanceView.levelOptions[performanceView.perf ? performanceView.perf.levelIndex : 1]
                                }
                                PerformanceRect {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 78
                                    theme: performanceView.theme
                                    label: "用户设置模式"
                                    textOverride: performanceView.perf ? performanceView.perf.preferredModeText : "—"
                                }
                                PerformanceRect {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 78
                                    theme: performanceView.theme
                                    label: "实际生效模式"
                                    textOverride: performanceView.perf ? performanceView.perf.activeModeText : "—"
                                }
                            }
                        }
                    }
                }

                //  中列：阶段计时
                SectionBox {
                    sectionTitle: performanceView.isSummary ? "阶段指标 · 会话汇总" : "阶段指标 · 实时窗口"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 460
                    Layout.minimumWidth: 300

                    // 监控关闭
                    Label {
                        anchors.centerIn: parent
                        visible: !performanceView.perfEnabled
                        text: "性能监控已关闭"
                        color: performanceView.theme.muted
                        font.pixelSize: 13
                    }

                    // 已启用但当前快照没有阶段数据
                    Label {
                        anchors.centerIn: parent
                        visible: performanceView.perfEnabled && performanceView.snapStages.length === 0
                        text: "暂无阶段数据\n(启动检测后出现)"
                        horizontalAlignment: Text.AlignHCenter
                        color: performanceView.theme.muted
                        font.pixelSize: 13
                    }

                    Flickable {
                        anchors.fill: parent
                        clip: true

                        contentWidth: width
                        contentHeight: stageGrid.implicitHeight

                        visible: performanceView.perfEnabled && performanceView.snapStages.length > 0

                        GridLayout {
                            id: stageGrid
                            width: parent.width
                            columns: 2
                            columnSpacing: 8
                            rowSpacing: 8
                            visible: parent.visible

                            Repeater {
                                model: performanceView.snapStages.length

                                PerformanceRect {
                                    required property int index
                                    readonly property var row: performanceView.snapStages[index]
                                    width: parent.width / 2 - 4
                                    height: 100
                                    theme: performanceView.theme
                                    label: row ? row.stageName : ""
                                    value: row ? row.averageMs : 0
                                    unit: "ms"
                                    caption: row ? ("最大 " + Number(row.maximumMs).toFixed(1) + " ms "
                                        + "\n平均 " + Number(row.averageMs).toFixed(1) + " ms"
                                        + "\n共 " + row.sampleCount + " 次") : ""
                                }
                            }
                        }
                    }
                }

                //  右列：内存占用
                SectionBox {
                visible: performanceView.memoryPanelVisible
                    sectionTitle: "内存占用"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 330
                    Layout.minimumWidth: 280

                    // 监控关闭
                    Label {
                        anchors.centerIn: parent
                        visible: !performanceView.perfEnabled
                        text: "性能监控已关闭"
                        color: performanceView.theme.muted
                        font.pixelSize: 13
                    }
                    // 插件未加载
                    Label {
                        anchors.centerIn: parent
                        visible: performanceView.perfEnabled
                            && performanceView.perf && !performanceView.perf.memoryMonitorAvailable
                        text: "内存监控插件不可用"
                        color: performanceView.theme.muted
                        font.pixelSize: 13
                    }
                    // 级别不足
                    Label {
                        anchors.centerIn: parent
                        visible: performanceView.perfEnabled
                            && performanceView.perf && performanceView.perf.memoryMonitorAvailable
                            && performanceView.perf.levelIndex < 3
                        text: "内存采样需「详细(Detailed)」及以上级别"
                        horizontalAlignment: Text.AlignHCenter
                        color: performanceView.theme.muted
                        font.pixelSize: 13
                    }

                    MemoryPanel {
                        anchors.fill: parent
                        visible: performanceView.memoryPanelVisible
                        perfApp: performanceView.perf
                        theme: performanceView.theme
                    }
                }
            }

            //  下层：日志
            SectionBox {
                id: logBox
                SplitView.fillWidth: true
                SplitView.preferredHeight: 160
                SplitView.minimumHeight: 40
                visible: performanceView.logVisible

                LogView {
                    id: logView
                    anchors.fill: parent
                    theme: performanceView.theme
                    closeEnable: false
                }
            }
        }
    }

    // 用于显示对应的性能计时详细描述
    Popup {
        id: perfInfoPopup
        modal: false
        focus: false
        closePolicy: Popup.CloseOnPressOutside
        anchors.centerIn: parent
        width: parent.width * 0.5
        height: parent.height * 0.7
        padding: 16

        background: Rectangle {
            radius: 12
            color: performanceView.theme.panel
            border.color: performanceView.theme.border
            border.width: 1
        }

        contentItem: Item {
            width: perfInfoPopup.availableWidth
            height: perfInfoPopup.availableHeight

            Flickable {
                id: infoFlick
                anchors.fill: parent
                contentWidth: width
                contentHeight: infoText.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                interactive: contentHeight > height
                ScrollBar.vertical: ScrollBar {}

                TextArea {
                    id: infoText
                    anchors.fill: parent
                    text: performanceView.stageInfoHtml
                    readOnly: true
                    selectByMouse: true
                    textFormat: TextEdit.RichText
                    wrapMode: TextEdit.Wrap
                    color: performanceView.theme.windowText
                    font.pixelSize: 13
                    leftPadding: 0
                    rightPadding: 28
                    topPadding: 0
                    bottomPadding: 0
                    background: null
                }
            }
        }
    }

    // 提醒重启应用弹窗
    MessageDialog {
        id: restartDialog
        visible: false
        property bool restarting: false
        property int pendingIndex: -1
        title: "关闭/启动性能监控需要重启应用"
        text: "为了使更改生效，需要重启应用。"
        buttons: MessageDialog.Ok | MessageDialog.Cancel
        onAccepted: {
            if (restartDialog.restarting)
                return;
            restartDialog.restarting = true;
            performanceView.perf.levelIndex = restartDialog.pendingIndex; // 确定设置级别
            appHelper.restart();
        }
        onRejected: {
            levelCombo.currentIndex = performanceView.perf ? performanceView.perf.levelIndex : 0;
        }
    }
}
