import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

// 配置Dialog
Dialog {
    id: dialog
    title: "修改配置"
    modal: true
    width: 580

    property var theme: null
    property var app: null			// DetectionApp
    property url modelPathUrl: ""	// 已选模型文件路径

    // 居中显示
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0

    background: Rectangle {
        color: dialog.theme ? dialog.theme.panel : "white"
        border.color: dialog.theme ? dialog.theme.border : "gray"
        border.width: 1
        radius: 6
    }

    // 用当前配置回填表单并打开
    function openWith(cfg, currentModelPathUrl) {
        // 如果配置存在，显示配置的信息
        if (cfg) {
            classNamesArea.text = cfg.classNames.join("\n");
            confSpin.value = Math.round(cfg.confThreshold * 100);
            iouSpin.value = Math.round(cfg.iouThreshold * 100);
            threadsSpin.value = cfg.numThreads;
            deviceSpin.value = cfg.deviceId;
            widthSpin.value = cfg.inputWidth;
            heightSpin.value = cfg.inputHeight;
            flowModeCombo.currentIndex = cfg.userModeTxt === "GPU" ? 1 : 0;
        }
        modelPathUrl = currentModelPathUrl ? currentModelPathUrl : "";
        open();
    }

    // 去掉前缀，显示本地路径
    function displayPath(u) {
        if (!u)
            return "";
        var s = u.toString();
        if (s.indexOf("file:///") === 0)
            s = s.substring(8);
        return decodeURIComponent(s);
    }
    // 配置表单
    contentItem: ColumnLayout {
        spacing: 10

        //  模型路径(.onnx)
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                text: "模型路径"
                Layout.preferredWidth: 80
                color: dialog.theme ? dialog.theme.windowText : "black"
            }
            TextField {
                Layout.fillWidth: true
                implicitWidth: 260
                implicitHeight: 32
                readOnly: true
                text: dialog.displayPath(dialog.modelPathUrl)
                placeholderText: "请选择 .onnx 模型文件"
                color: dialog.theme ? dialog.theme.text : "black"
                background: Rectangle {
                    implicitWidth: 260
                    implicitHeight: 32
                    color: dialog.theme ? dialog.theme.base : "white"
                    border.color: dialog.theme ? dialog.theme.border : "gray"
                    border.width: 1
                    radius: 4
                }
            }
            Button {
                text: "浏览…"
                onClicked: modelFileDialog.open()
            }
        }

        //  类别名
        Label {
            text: "类别名（每行一个，或用逗号分隔）"
            color: dialog.theme ? dialog.theme.windowText : "black"
        }
        //  类别名（ScrollView 提供滚动，内容超出时可用滚轮/滚动条下拉）
        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: 84
            clip: true
            background: Rectangle {
                color: dialog.theme ? dialog.theme.base : "white"
                border.color: dialog.theme ? dialog.theme.border : "gray"
                border.width: 1
                radius: 4
            }

            TextArea {
                id: classNamesArea
                placeholderText: "例如： 人，牛，猴子……"
                wrapMode: TextArea.Wrap
                selectByMouse: true
                color: dialog.theme ? dialog.theme.text : "black"
                background: null
            }
        }

        //  阈值
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                text: "置信度阈值"
                Layout.preferredWidth: 80
                color: dialog.theme ? dialog.theme.windowText : "black"
            }
            SpinBox {
                id: confSpin
                from: 1
                to: 99
                editable: true
                value: 25
                readonly property real realValue: value / 100
                textFromValue: function (v) {
                    return (v / 100).toFixed(2);
                }
                valueFromText: function (t) {
                    return Math.round(parseFloat(t) * 100);
                }
            }
            Item {
                Layout.fillWidth: true
            }
            Label {
                text: "NMS 阈值"
                Layout.preferredWidth: 80
                color: dialog.theme ? dialog.theme.windowText : "black"
            }
            SpinBox {
                id: iouSpin
                from: 1
                to: 99
                editable: true
                value: 70
                readonly property real realValue: value / 100
                textFromValue: function (v) {
                    return (v / 100).toFixed(2);
                }
                valueFromText: function (t) {
                    return Math.round(parseFloat(t) * 100);
                }
            }
        }

        //线程数 / CUDA 设备号
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                text: "线程数"
                Layout.preferredWidth: 80
                color: dialog.theme ? dialog.theme.windowText : "black"
            }
            SpinBox {
                id: threadsSpin
                from: 0
                to: 64
                value: 0
                editable: true
            }
            Item {
                Layout.fillWidth: true
            }
            Label {
                text: "CUDA 设备号"
                Layout.preferredWidth: 80
                color: dialog.theme ? dialog.theme.windowText : "black"
            }
            SpinBox {
                id: deviceSpin
                from: 0
                to: 15
                value: 0
                editable: true
            }
        }

        //  输入尺寸
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                text: "输入宽度"
                Layout.preferredWidth: 80
                color: dialog.theme ? dialog.theme.windowText : "black"
            }
            SpinBox {
                id: widthSpin
                from: 32
                to: 4096
                value: 640
                stepSize: 32
                editable: true
            }
            Item {
                Layout.fillWidth: true
            }
            Label {
                text: "输入高度"
                Layout.preferredWidth: 80
                color: dialog.theme ? dialog.theme.windowText : "black"
            }
            SpinBox {
                id: heightSpin
                from: 32
                to: 4096
                value: 640
                stepSize: 32
                editable: true
            }
        }

        //  流程模式
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                text: "流程模式"
                color: dialog.theme ? dialog.theme.windowText : "black"
            }
            ColumnLayout {
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Label {
                        Layout.preferredWidth: 80
                        text: "实际生效模式："
                        color: dialog.theme ? dialog.theme.windowText : "black"
                    }
                    Label {
                        Layout.preferredWidth: 80
                        text: dialog.app ? dialog.app.config.effectiveModeTxt : "无效模式"
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Label {
                        text: "选择模式："
                        Layout.preferredWidth: 80
                        color: dialog.theme ? dialog.theme.windowText : "black"
                    }
                    ComboBox {
                        id: flowModeCombo
                        Layout.fillWidth: true
                        currentIndex: 0
                        model: ["CPU", "GPU"]
                    }
                }
            }
        }
    }

    footer: DialogButtonBox {
        Button {
            id: cancelButton
            text: "取消"
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
        Button {
            id: acceptButton
            focus: true
            text: "确定"
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }
    }
    // 确定后将配置数据交给Application
    onAccepted: {
        if (!app)
            return;
        // 类别名按换行 / 逗号切分，并去掉空白项
        var names = [];
        var parts = classNamesArea.text.split(/[\n,，]/);
        for (var i = 0; i < parts.length; ++i) {
            var s = parts[i].trim();
            if (s.length > 0)
                names.push(s);
        }
        app.setConfig(dialog.modelPathUrl, names, confSpin.realValue, iouSpin.realValue, threadsSpin.value, deviceSpin.value, widthSpin.value, heightSpin.value, flowModeCombo.currentText);
    }
    Shortcuts {
        targetItem: dialog
        acceptButton: acceptButton
        cancelButton: cancelButton
    }

    // 选择 onnx 模型文件
    FileDialog {
        id: modelFileDialog
        title: "选择 ONNX 模型"
        fileMode: FileDialog.OpenFile
        nameFilters: ["ONNX 模型 (*.onnx)", "所有文件 (*)"]
        onAccepted: dialog.modelPathUrl = selectedFile
    }
}
