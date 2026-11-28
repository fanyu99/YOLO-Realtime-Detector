import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

// 内存占用面板
Item {
    id: panel

    property var perfApp: null  // PerformanceApp
    property var theme: null    // 主题

    readonly property var snapshot: panel.perfApp
        ? (panel.perfApp.isRunning
            ? panel.perfApp.memorySnapshot
            : panel.perfApp.overallMemorySnapshot.snapshot)
        : null
    readonly property bool available: panel.perfApp
        ? panel.perfApp.memoryMonitorAvailable : false
    readonly property bool summarizing: panel.perfApp ? !panel.perfApp.isRunning : false

    // 总量整圆与占用扇形的配色
    readonly property color totalCircleColor: "#2E7D32"   // 总内存/总显存：深绿整圆
    readonly property color fillHealthyColor: "#34d55f"
    readonly property color fillWarningColor: "#F59E0B"
    readonly property color fillDangerColor: "#EF4444"

    implicitWidth: 280
    implicitHeight: 420

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        // 内存占用
        Item {
            id: memoryShower
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 100

            readonly property real percent: {
                if (!panel.available || !panel.snapshot)
                    return 0;
                const total = panel.snapshot.totalMemory;
                return total > 0 ? Math.min(1, panel.snapshot.processMemory / total) : 0;
            }
            readonly property string centerText: panel.available
                ? "内存占用 " + Math.round(percent * 100) + "%" : "内存不可用"
            readonly property bool hasTotal: panel.available
                && panel.snapshot && panel.snapshot.totalMemory > 0

            onPercentChanged: canvas1.requestPaint()
            onHasTotalChanged: canvas1.requestPaint()

            Canvas {
                id: canvas1
                anchors.fill: parent
                antialiasing: true
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()

                onPaint: {
                    const ctx = getContext("2d");
                    const cx = width / 2;
                    const cy = height / 2;
                    const r = Math.min(width, height) / 2 - 6;
                    ctx.clearRect(0, 0, width, height);
                    if (r <= 0)
                        return;

                    const p = Math.max(0, Math.min(1, memoryShower.percent));
                    // 总内存整圆作为底，进程占用扇形叠加在上面
                    if (memoryShower.hasTotal) {
                        ctx.beginPath();
                        ctx.arc(cx, cy, r, 0, 2 * Math.PI);
                        ctx.fillStyle = panel.totalCircleColor;
                        ctx.fill();
                    }
                    if (p > 0) {
                        const start = -Math.PI / 2;
                        ctx.beginPath();
                        ctx.moveTo(cx, cy);
                        ctx.arc(cx, cy, r, start, start + p * 2 * Math.PI, false);
                        ctx.closePath();
                        ctx.fillStyle = p > 0.9 ? panel.fillDangerColor
                            : p > 0.75 ? panel.fillWarningColor : panel.fillHealthyColor;
                        ctx.fill();
                    }

                    if (memoryShower.centerText !== "") {
                        ctx.fillStyle = panel.theme ? panel.theme.windowText : "#333333";
                        ctx.font = Math.max(12, r * 0.22) + "px sans-serif";
                        ctx.textAlign = "center";
                        ctx.textBaseline = "middle";
                        ctx.fillText(memoryShower.centerText, cx, cy);
                    }
                }

                ToolTip {
                    id: tip1
                    text: panel.available && panel.snapshot
                        ? memoryShower.centerText + "\n"
                            + "进程内存: " + Number(panel.snapshot.processMemoryGb).toFixed(2) + " GB\n"
                            + "系统总内存: " + Number(panel.snapshot.totalMemoryGb).toFixed(2) + " GB\n"
                            + "系统可用: " + Number(panel.snapshot.availableMemoryGb).toFixed(2) + " GB\n"
                            + "系统已用: " + Number(panel.snapshot.totalMemoryGb - panel.snapshot.availableMemoryGb).toFixed(2) + " GB"
                            + (panel.summarizing
                                ? "\n本次峰值: " + Number(panel.perfApp.overallMemorySnapshot.peakProcessMemoryGb).toFixed(2) + " GB"
                                : "")
                        : "内存监控插件不可用"
                    delay: 400
                    timeout: 5000
                }

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onContainsMouseChanged: tip1.visible = containsMouse
                }
            }
        }

        // 显存占用
        Item {
            id: vramShower
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 100

            readonly property real percent: {
                if (!panel.available || !panel.snapshot)
                    return 0;
                const total = panel.snapshot.totalGpuMemory;
                return total > 0 ? Math.min(1, panel.snapshot.processGpuMemory / total) : 0;
            }
            readonly property string centerText: panel.available
                ? "显存占用 " + Math.round(percent * 100) + "%" : "显存不可用"
            readonly property bool hasTotal: panel.available
                && panel.snapshot && panel.snapshot.totalGpuMemory > 0

            onPercentChanged: canvas2.requestPaint()
            onHasTotalChanged: canvas2.requestPaint()

            Canvas {
                id: canvas2
                anchors.fill: parent
                antialiasing: true
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()

                onPaint: {
                    const ctx = getContext("2d");
                    const cx = width / 2;
                    const cy = height / 2;
                    const r = Math.min(width, height) / 2 - 6;
                    ctx.clearRect(0, 0, width, height);
                    if (r <= 0)
                        return;

                    const p = Math.max(0, Math.min(1, vramShower.percent));
                    // 总显存整圆作为底，进程占用扇形叠加在上面
                    if (vramShower.hasTotal) {
                        ctx.beginPath();
                        ctx.arc(cx, cy, r, 0, 2 * Math.PI);
                        ctx.fillStyle = panel.totalCircleColor;
                        ctx.fill();
                    }
                    if (p > 0) {
                        const start = -Math.PI / 2;
                        ctx.beginPath();
                        ctx.moveTo(cx, cy);
                        ctx.arc(cx, cy, r, start, start + p * 2 * Math.PI, false);
                        ctx.closePath();
                        ctx.fillStyle = p > 0.9 ? panel.fillDangerColor
                            : p > 0.75 ? panel.fillWarningColor : panel.fillHealthyColor;
                        ctx.fill();
                    }

                    if (vramShower.centerText !== "") {
                        ctx.fillStyle = panel.theme ? panel.theme.windowText : "#333333";
                        ctx.font = Math.max(12, r * 0.22) + "px sans-serif";
                        ctx.textAlign = "center";
                        ctx.textBaseline = "middle";
                        ctx.fillText(vramShower.centerText, cx, cy);
                    }
                }

                ToolTip {
                    id: tip2
                    text: panel.available && panel.snapshot
                        ? vramShower.centerText + "\n"
                            + "进程显存: " + Number(panel.snapshot.processGpuMemoryGb).toFixed(2) + " GB\n"
                            + "显存总量: " + Number(panel.snapshot.totalGpuMemoryGb).toFixed(2) + " GB\n"
                            + "显存可用: " + Number(panel.snapshot.availableGpuMemoryGb).toFixed(2) + " GB"
                            + (panel.summarizing
                                ? "\n本次峰值: " + Number(panel.perfApp.overallMemorySnapshot.peakProcessGpuMemoryGb).toFixed(2) + " GB"
                                : "")
                        : "内存监控插件不可用"
                    delay: 400
                    timeout: 5000
                }

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onContainsMouseChanged: tip2.visible = containsMouse
                }
            }
        }
    }
}
