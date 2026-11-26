import QtQuick
import QtQuick.Templates as T

//  快捷键组
//    确定 Return / Enter  
//    取消 Esc             
//    停止 q               
Item {
    id: root
    visible: false

    property var targetItem: null		// 生效目标(Dialog / 面板根)
    property bool active: true			// 额外生效条件，例如检测正在运行
    property T.AbstractButton acceptButton: null
    property T.AbstractButton cancelButton: null
    property T.AbstractButton stopButton: null

    function canTrigger(button) {
        return root.active
            && root.targetItem !== null && root.targetItem.visible
            && button !== null && button.enabled;
    }

    Shortcut {
        sequences: ["Return", "Enter"]
        enabled: root.canTrigger(root.acceptButton)
        onActivated: root.acceptButton.click()
    }

    Shortcut {
        sequence: "Esc"
        enabled: root.canTrigger(root.cancelButton)
        onActivated: root.cancelButton.click()
    }

    Shortcut {
        sequence: "q"
        enabled: root.canTrigger(root.stopButton)
        onActivated: root.stopButton.click()
    }
}
