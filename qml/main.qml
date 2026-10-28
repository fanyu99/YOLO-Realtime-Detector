import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtMultimedia

// ============================================================================
//  检测头 —— 主界面
//  顶部：标题 / 主题指示（主题跟随系统，自动浅色 / 深色）
//  左侧：常驻配置栏，展示 DetectionApp.config
//  右侧：工具栏 + 检测结果显示区 + 日志区
// ============================================================================
Window {
	id: window
	visible: true
	width: 1200
	height: 820
	minimumWidth: 900
	minimumHeight: 600
	title: "检测头"
	color: theme.window

	// 主题(跟随系统) 与 后端门面
	Theme { id: theme }
	DetectionApp { id: app } // 后端App,负责将QML与C++桥接，提供配置、日志、检测等功能

	// ---- 界面状态 ----
	property string selectedFileName: ""		// 已选文件名(仅显示)
	property url selectedFileUrl: ""			// 已选文件 url(供图片 / 视频预览)
	property bool videoMode: false				// 当前选择模式：false=图片, true=视频

	// ---- 状态栏(与日志同源：订阅 Logger.messageLogged) ----
	property int statusLevel: 1					// 当前状态级别(默认 INFO)
	property string statusText: "就绪"			// 当前状态文本
	property string statusTime: ""				// 状态时间

	// 从 url 中取出文件名
	function fileNameFromUrl(u) {
		var s = u.toString()
		var i = s.lastIndexOf('/')
		return i >= 0 ? s.substring(i + 1) : s
	}

	// 把"保存开关 + 目录"一起交给后端。后端在开关关闭时会忽略目录，
	// 所以这里不需要自己拼 detection_results —— 那是后端的约定。
	function applySaveOptions() {
		app.setSaveOptions(saveCheck.checked, saveDirDialog.selectedFolder)
	}

	ColumnLayout {
		anchors.fill: parent
		spacing: 0

		// ====================================================================
		//  顶部标题栏
		// ====================================================================
		ToolBar {
			Layout.fillWidth: true
			RowLayout {
				anchors.fill: parent
				anchors.leftMargin: 8
				anchors.rightMargin: 8
				Label {
					text: "检测头"
					font.bold: true
					font.pixelSize: 15
					Layout.fillWidth: true
				}
				Label {
					text: theme.dark ? "主题：深色（跟随系统）" : "主题：浅色（跟随系统）"
					opacity: 0.75
				}
			}
		}

		// ====================================================================
		//  横幅（一条槽位，按级别换配色，避免多条堆叠）
		//  警告级：非致命提示，典型场景"输入尺寸已按模型自动对齐"——不能静默处理。
		//  成功级：结果落盘后告知路径与文件数。
		//  bannerText 为空时整条隐藏（高度归零，不占布局）。
		// ====================================================================
		Rectangle {
			id: warningBanner
			Layout.fillWidth: true
			Layout.preferredHeight: visible ? 34 : 0
			visible: app.bannerText.length > 0
			readonly property bool isSuccess: app.bannerLevel === DetectionApp.BannerSuccess
			color: isSuccess
				   ? (theme.dark ? "#0d3321" : "#e3f7ec")
				   : (theme.dark ? "#3d3208" : "#fff4d6")
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
					text: app.bannerText
					elide: Text.ElideMiddle
					color: warningBanner.isSuccess
						   ? (theme.dark ? "#9fe1cb" : "#0f5132")
						   : (theme.dark ? "#ffd9a0" : "#7a4a00")
					font.pixelSize: 12
				}

				Button {
					text: "知道了"
					flat: true
					onClicked: app.dismissBanner()
				}
			}
		}

		// ====================================================================
		//  主体：左侧配置栏 + 右侧主区
		// ====================================================================
		RowLayout {
			Layout.fillWidth: true
			Layout.fillHeight: true
			spacing: 0

			// ---------------- 左侧常驻配置栏 ----------------
			Frame {
				id: sidebar
				Layout.preferredWidth: 250
				Layout.fillHeight: true
				padding: 10
				// 侧边栏用面板色，与主区区分开
				background: Rectangle {
					color: theme.panel
					border.color: theme.border
					border.width: 1
				}

				ColumnLayout {
					anchors.fill: parent
					spacing: 8

					Label {
						text: "当前配置"
						font.bold: true
						font.pixelSize: 14
					}

					// 配置项列表：标签 + 值(跟随 app.config 自动刷新)
					Column {
						Layout.fillWidth: true
						spacing: 8

						Repeater {
							model: [
								{ "k": "模型",      "v": app.config.modelPath },
								{ "k": "类别",      "v": app.config.classNames.join(", ") },
								{ "k": "置信度",    "v": app.config.confThreshold.toFixed(2) },
								{ "k": "NMS",       "v": app.config.iouThreshold.toFixed(2) },
								{ "k": "输入尺寸",  "v": app.config.inputWidth + "×" + app.config.inputHeight },
								{ "k": "线程数",    "v": String(app.config.numThreads) },
								{ "k": "设备号",    "v": String(app.config.deviceId) },
								{ "k": "启用 CUDA", "v": app.config.useCuda ? "是" : "否" },
								{ "k": "模型存在",  "v": app.config.modelExists ? "是" : "否" }
							]

							delegate: Column {
								width: parent.width
								spacing: 0
								Label {
									text: modelData.k
									opacity: 0.6
									font.pixelSize: 12
								}
								Label {
									text: (modelData.v && modelData.v.length > 0) ? modelData.v : "—"
									wrapMode: Text.Wrap
									width: parent.width
									font.pixelSize: 12
								}
							}
						}
					}

					Item { Layout.fillHeight: true }

					// ---------------- 结果输出 ----------------
					// 勾选后把"画了框的结果"写到 <目录>/detection_results/。
					// 批量作业建议勾选：否则结果只在界面里过一遍，无法留档。
					// 视频写成 mp4，图片写成 jpg；不写 labels（用途是视觉查看）。
					ColumnLayout {
						Layout.fillWidth: true
						spacing: 4

						Label {
							text: "结果输出"
							font.bold: true
							font.pixelSize: 14
						}

						CheckBox {
							id: saveCheck
							text: "保存推理结果"
							enabled: !app.busy
							onToggled: {
								// 还没选目录就先弹目录选择，选完自动勾上
								if (checked && saveDirDialog.selectedFolder.toString().length === 0) {
									checked = false
									saveDirDialog.open()
								} else {
									window.applySaveOptions()
								}
							}
						}

						RowLayout {
							Layout.fillWidth: true
							spacing: 4

							Label {
								Layout.fillWidth: true
								elide: Text.ElideMiddle
								font.pixelSize: 12
								opacity: app.saveDir.toString().length > 0 ? 1.0 : 0.55
								text: app.saveDir.toString().length > 0
									  ? window.fileNameFromUrl(app.saveDir)
									  : "未选择保存目录"
							}

							Button {
								text: "选择…"
								enabled: !app.busy
								onClicked: saveDirDialog.open()
							}
						}
					}

					// 状态栏：显示最近一条日志(左侧色条与文字颜色随日志级别变化)
					Rectangle {
						Layout.fillWidth: true
						Layout.preferredHeight: 46
						color: theme.base
						border.color: theme.border
						border.width: 1
						radius: 4

						RowLayout {
							anchors.fill: parent
							anchors.margins: 6
							spacing: 6

							// 级别色条
							Rectangle {
								Layout.preferredWidth: 4
								Layout.fillHeight: true
								radius: 2
								color: theme.levelColor(window.statusLevel)
							}

							ColumnLayout {
								Layout.fillWidth: true
								spacing: 0

								Label {
									text: window.statusTime.length > 0 ? ("状态 · " + window.statusTime) : "状态"
									opacity: 0.6
									font.pixelSize: 11
								}
								Label {
									Layout.fillWidth: true
									text: window.statusText
									wrapMode: Text.Wrap
									maximumLineCount: 2
									elide: Text.ElideRight
									color: theme.levelColor(window.statusLevel)
									font.pixelSize: 12
								}
							}
						}
					}

					// 开始检测(推理接入 UI 后生效)
					Button {
						text: "开始检测"
						Layout.fillWidth: true
						onClicked: app.onDetectClicked()
					}
				}
			}

			// ---------------- 右侧主区 ----------------
			ColumnLayout {
				Layout.fillWidth: true
				Layout.fillHeight: true
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
							enabled: !app.busy
							onClicked: fileMenu.open()
						}

						// 已选文件名
						Label {
							Layout.fillWidth: true
							Layout.minimumWidth: 120
							elide: Text.ElideMiddle
							text: window.selectedFileName.length > 0
								  ? "已选文件：" + window.selectedFileName
								  : "未选择文件"
							opacity: window.selectedFileName.length > 0 ? 1.0 : 0.55
						}

						// 摄像头检测(先弹对话框选设备 id)
						Button {
							text: "摄像头检测"
							enabled: !app.busy
							onClicked: cameraDialog.open()
						}

						// 海康工业相机检测(先弹对话框选设备；走 MVS SDK，与上面的 USB 编号不是一回事)
						Button {
							text: "海康相机检测"
							enabled: !app.busy
							onClicked: hikCameraDialog.open()
						}

						// 批量检测(多选图片/视频，一个作业顺序跑完再汇总)
						Button {
							text: "批量检测"
							enabled: !app.busy
							onClicked: batchDialog.open()
						}

						// 停止当前检测(仅作业运行中可用)
						Button {
							text: "停止检测"
							enabled: app.busy
							onClicked: app.cancelCurrentJob()
						}

						// 修改配置(弹出模态配置对话框)
						Button {
							text: "修改配置"
							highlighted: true
							enabled: !app.busy
							onClicked: configDialog.openWith(app.config, app.modelPath)
						}
					}

					// 文件选择菜单(挂到按钮下方弹出)
					Menu {
						id: fileMenu
						parent: fileSelectBtn
						y: fileSelectBtn.height
						MenuItem {
							text: "选择图片…"
							onTriggered: { window.videoMode = false; fileDialog.open() }
						}
						MenuItem {
							text: "选择视频…"
							onTriggered: { window.videoMode = true; fileDialog.open() }
						}
					}
				}

				// ---------- 显示区 + 日志区 ----------
				SplitView {
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

								Label {
									text: "检测结果显示"
									font.bold: true
								}

								// 进度：摄像头这类总量未知的走忙碌态
								ProgressBar {
									Layout.fillWidth: true
									Layout.leftMargin: 12
									from: 0
									to: 1
									value: app.progress
									indeterminate: app.progressIndeterminate
									visible: app.busy
								}

								// ---- 结果回看 ----
								// 只推进 UI 侧的索引，**不阻塞 worker**：推理照常全速跑完，
								// 看得慢只是有界缓冲里少留几帧。所以"下一个"是即时响应的，
								// 取消 / 停止 / 关窗也都不受影响。
								Label {
									text: app.resultInfo
									opacity: 0.6
									font.pixelSize: 12
								}

								Button {
									text: "上一个"
									enabled: app.resultCount > 0
											 && (!app.reviewing || app.resultIndex > 0)
									onClicked: app.prevResult()
								}

								Button {
									text: "下一个"
									enabled: app.resultCount > 0
											 && (!app.reviewing || app.resultIndex < app.resultCount - 1)
									onClicked: app.nextResult()
								}

								Button {
									text: "回到实时"
									visible: app.reviewing
									onClicked: app.followLatest()
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

								// 检测结果画面：图片 / 视频 / 摄像头统一走这条路
								// source 必须带上会变化的 rev，否则 QML 会因为 URL 完全相同而吃缓存；
								// cache 必须为 false，否则每个 rev 都会在图片缓存里留一份，内存只涨不降；
								// asynchronous 必须为 false，否则异步取图会乱序，画面会跳帧。
								Image {
									id: detectionImage
									anchors.fill: parent
									anchors.margins: 4
									fillMode: Image.PreserveAspectFit
									cache: false
									asynchronous: false
									visible: app.frameRevision > 0
									source: app.frameRevision > 0 ? app.frameSource() : ""
								}

								// 尚未开始检测时的图片预览
								Image {
									id: resultImage
									anchors.fill: parent
									anchors.margins: 4
									fillMode: Image.PreserveAspectFit
									cache: false
									asynchronous: true
									visible: !window.videoMode && app.frameRevision === 0
								}

								// 尚未开始检测时的视频预览
								VideoOutput {
									id: videoOutput
									anchors.fill: parent
									anchors.margins: 4
									fillMode: VideoOutput.PreserveAspectFit
									visible: window.videoMode && app.frameRevision === 0
								}

								// 占位提示
								Label {
									anchors.centerIn: parent
									horizontalAlignment: Text.AlignHCenter
									text: "尚无检测结果\n请选择图片 / 视频，或启动摄像头检测"
									color: theme.muted
									visible: window.selectedFileName.length === 0 && app.frameRevision === 0
								}
							}
						}
					}

					// ---- 日志区 ----
					// 性能面板已移除：阶段级耗时改由 tests/test_performance 测量，
					// 日常使用只需要画面左上角的 fps 与结果里的推理耗时(worker 侧已直接产出)。
					LogView {
						theme: theme
						SplitView.preferredHeight: 190
						SplitView.minimumHeight: 90
					}
				}
			}
		}
	}

	// ========================================================================
	//  视频播放器：只用于"检测之前"预览所选视频。
	//  检测一旦开始，画面改由检测帧提供(detectionImage)，这里主动暂停，
	//  避免两路视频同时解码。
	// ========================================================================
	MediaPlayer {
		id: mediaPlayer
		source: window.selectedFileUrl
		videoOutput: videoOutput
		onErrorOccurred: function(error, errorString) {
			Logger.warning("[UI]: 视频播放失败: " + errorString)
		}
	}

	// 检测开始后暂停原始视频预览
	Connections {
		target: app
		function onBusyChanged() {
			if (app.busy && window.videoMode)
				mediaPlayer.pause()
		}
	}

	// ========================================================================
	//  文件选择对话框
	// ========================================================================
	FileDialog {
		id: fileDialog
		title: window.videoMode ? "选择视频文件" : "选择图片文件"
		fileMode: FileDialog.OpenFile
		nameFilters: window.videoMode
			? ["视频文件 (*.mp4 *.avi *.mkv *.mov *.wmv)", "所有文件 (*)"]
			: ["图片文件 (*.jpg *.jpeg *.png *.bmp *.webp)", "所有文件 (*)"]
		onAccepted: {
			window.selectedFileName = window.fileNameFromUrl(selectedFile)
			app.setFilePath(selectedFile)			// 把文件路径交给后端
			if (window.videoMode) {
				window.selectedFileUrl = selectedFile
				mediaPlayer.play()
			} else {
				window.selectedFileUrl = ""			// 切回图片时停掉视频
				resultImage.source = selectedFile
			}
			Logger.info("[UI]: 已选择" + (window.videoMode ? "视频" : "图片") + "：" + window.selectedFileName)
		}
		onRejected: Logger.info("[UI]: 已取消选择文件")
	}

	// 批量检测：多选图片/视频，交给一个作业顺序跑完再汇总。
	// 传 url 列表给后端，路径转换在 C++ 侧用 toLocalFile() 做。
	FileDialog {
		id: batchDialog
		title: "选择要批量检测的文件（可多选）"
		fileMode: FileDialog.OpenFiles
		nameFilters: [
			"图片与视频 (*.jpg *.jpeg *.png *.bmp *.webp *.mp4 *.avi *.mov *.mkv *.wmv)",
			"所有文件 (*)"
		]
		onAccepted: {
			if (selectedFiles.length === 0)
				return
			Logger.info("[UI]: 提交批量检测，文件数：" + selectedFiles.length)
			app.submitBatch(selectedFiles)
		}
		onRejected: Logger.info("[UI]: 已取消批量选择")
	}

	// 结果保存目录选择
	FolderDialog {
		id: saveDirDialog
		title: "选择结果保存目录"
		onAccepted: {
			saveCheck.checked = true		// 选完目录自动勾上保存
			window.applySaveOptions()
			Logger.info("[UI]: 结果将保存到该目录下的 detection_results/")
		}
	}

	// 三个模态对话框
	ConfigDialog { id: configDialog; theme: theme; app: app }
	CameraDialog { id: cameraDialog; theme: theme; app: app }
	HikCameraDialog { id: hikCameraDialog; theme: theme; app: app }

	// 状态栏订阅日志流：与日志列表同源，
	// 选择文件 / 配置更新 / 错误 / 检测完成等都会自动刷新状态栏
	Connections {
		target: Logger
		function onMessageLogged(level, levelName, time, message) {
			window.statusLevel = level
			window.statusTime = time
			window.statusText = message
		}
	}

	Component.onCompleted: Logger.info("[UI]: 界面已就绪，等待选择图片 / 视频")
}
