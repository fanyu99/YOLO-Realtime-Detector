import QtQuick
import QtQuick.Dialogs

// 文件选择dialog
//    "video" / "image" / "both"
FileDialog {
    id: root

    property string category: "video" // 文件类别
    property bool multiSelect: false // 是否多选
    property string dialogTitle: ""		// 标题
    // 获取选择的文件名
    readonly property string selectedFileName: {
        var s = selectedFile.toString();
        var i = s.lastIndexOf('/');
        return i >= 0 ? s.substring(i + 1) : s;
    }

    function filtersFor(cat) {
        switch (cat) {
        case "video":
            return ["视频文件 (*.mp4 *.avi *.mkv *.mov *.wmv)", "所有文件 (*)"];
        case "image":
            return ["图片文件 (*.jpg *.jpeg *.png *.bmp *.webp)", "所有文件 (*)"];
        case "both":
            return ["图片与视频 (*.jpg *.jpeg *.png *.bmp *.webp *.mp4 *.avi *.mov *.mkv *.wmv)", "所有文件 (*)"];
        default:
            return ["所有文件 (*)"];
        }
    }

    function titleFor(cat) {
        switch (cat) {
        case "video":
            return "选择视频文件";
        case "image":
            return "选择图片文件";
        case "both":
            return "选择图片或视频文件";
        default:
            return "选择文件";
        }
    }

    title: dialogTitle.length > 0 ? dialogTitle : titleFor(category)
    fileMode: multiSelect ? FileDialog.OpenFiles : FileDialog.OpenFile
    nameFilters: filtersFor(category)
}
