import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

// 海康相机选择对话框(模态)
Dialog {
    id: dialog
    title: "选择海康相机"
    modal: true
    width: 420

    property var theme: null	// 主题
    property var app: null		

    // 枚举结果，元素为 { serial, model, userDefinedName, online }
    property var devices: []
    readonly property bool hasSelection: deviceList.currentIndex >= 0

    function currentDevice() {
        return hasSelection ? devices[deviceList.currentIndex] : null;
    }
    Connections {
        target: app
        function onLoadingResult(result) {
            dialog.devices = result;
            // 只有一台设备时直接选中
            deviceList.currentIndex = dialog.devices.length > 0 ? 0 : -1;
        }
    }

    function refresh() {
        dialog.devices=[]
        if (app)
            app.hikDevices();
    }

    // 每次打开都重新枚举
    onOpened: refresh()

    // 居中显示
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0

    background: Rectangle {
        color: dialog.theme ? dialog.theme.panel : "white"
        border.color: dialog.theme ? dialog.theme.border : "gray"
        border.width: 1
        radius: 6
    }

    contentItem: ColumnLayout {
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: dialog.devices.length > 0 ? "已发现 " + dialog.devices.length + " 台设备，请选择：" : "未发现在线设备"
                color: dialog.theme ? dialog.theme.windowText : "black"
            }

            ToolButton {
                text: "刷新"
                enabled: dialog.app !== null
                onClicked: dialog.refresh()
            }
        }

        ListView {
            id: deviceList
            Layout.fillWidth: true
            Layout.preferredHeight: 180
            clip: true
            spacing: 4
            visible: dialog.devices.length > 0
            model: dialog.devices

            delegate: Rectangle {
                required property int index
                required property var modelData

                width: ListView.view.width
                height: 52
                radius: 4
                color: dialog.theme ? dialog.theme.base : "white"
                border.width: ListView.isCurrentItem ? 2 : 1
                border.color: ListView.isCurrentItem ? (dialog.theme ? dialog.theme.accent : "steelblue") : (dialog.theme ? dialog.theme.border : "gray")

                Column {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2

                    Label {
                        width: parent.width
                        elide: Text.ElideRight
                        text: modelData.userDefinedName && modelData.userDefinedName.length > 0 ? modelData.userDefinedName : (modelData.model || "(未知型号)")
                        color: dialog.theme ? dialog.theme.windowText : "black"
                    }

                    Label {
                        width: parent.width
                        elide: Text.ElideRight
                        text: "序列号：" + (modelData.serial || "—") + (modelData.model ? "  ·  " + modelData.model : "")
                        font.pixelSize: 11
                        opacity: 0.75
                        color: dialog.theme ? dialog.theme.muted : "gray"
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: deviceList.currentIndex = index
                    onDoubleClicked: {
                        deviceList.currentIndex = index;
                        dialog.accept();			// 双击直接开始
                    }
                }
            }
         BusyIndicator{
                id: loadingIndicator
                running: dialog.app ? dialog.app.loading : false
                anchors.centerIn: parent
                width : 50
                height :50
             }
        }

        // 空显示
        Label {
            Layout.fillWidth: true
            Layout.preferredHeight: 180
            visible: dialog.devices.length === 0
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.WordWrap
            color: dialog.theme ? dialog.theme.muted : "gray"
            text: "没有枚举到海康相机。\n请确认相机已上电、网线或 USB 已连接、" + "且已安装 MVS 运行时，然后点「刷新」。"
        }
    }
    Label {
        text: "选择触发模式："
        color: dialog.theme ? dialog.theme.windowText : "black"
    }
    ComboBox {
        id: modeComboBox
        Layout.fillWidth: true
        model: ["off", "software", "line0", "line1", "line2"]
        currentIndex: 0
    }

    footer: DialogButtonBox {
        Button {
            text: "刷新"
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: dialog.refresh()
        }
        Button {
            id: cancelButton
            text: "取消"
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
        Button {
            id: acceptButton
            text: "确定"
            enabled: dialog.hasSelection && ! loadingIndicator.running
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }
    }
    Shortcuts {
        targetItem: dialog
        acceptButton: acceptButton
        cancelButton: cancelButton
    }

    // 点击「确定」：把序列号交给后端
    onAccepted: {
        const device = currentDevice();
        if (device && dialog.app)
            dialog.app.onDetectHikCameraClicked(device.serial, modeComboBox.currentValue);
    }
}
