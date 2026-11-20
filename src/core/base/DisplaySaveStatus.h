#pragma once
#include <QString>
#include "DisplayPolicy.h"

// 作业的显示/保存需求
// App通过选择的模式+当前的流程模式+是否保存结果进行推导出最后的用于判断是否要渲染、QImage、快照、下载
struct DisplaySaveStatus {
	// ---- 保存 ----
	QString saveDir; // 保存目录，空不保存

	// 后端显示模式和实际生效的预览模式
	DisplayBackend backend = DisplayBackend::ImageProvider;    // 显示后端
	ViewMode activeMode = ViewMode::Offline;                   // 实际生效的预览模式

	// ---- 显示 ----
	bool renderBoxes = false;   // 是否需要画检测框（离线、保存需要）
	bool needQImage = false;  // 是否需要QImage回看
	// 是否需要快照（送到overlay）
	bool needSnapshot = false;
	// 当前是否需要下载到CPU，后续接入GPUOverlay进行废弃
	bool download2CPU = false;


	// 是否保存结果
	bool isSaveResult () const { return !saveDir.isEmpty (); }

	//   userMode          用户选择的预览模式
	//   gpuFlow           本次作业的实际流程模式是否为 GPU
	//   overlayAvailable  场景里是否真的有叠加层消费者(DetectionOverlayItem 自注册)
	//   resultsDir        已组合好的结果目录(DetectionResultSaver::resultsDirFor)，空 = 不保存
	static DisplaySaveStatus getDisplaySaveStatus ( ViewMode userMode, bool gpuFlow,
		bool overlayAvailable, const QString& resultsDir ) {
		DisplaySaveStatus status;
		status.saveDir = resultsDir;
		status.activeMode = userMode;
		status.backend = DisplayPolicy::backendFor ( userMode );

		// 如果选择实时但是不支持Overlay，降级为离线模式
		if (status.backend == DisplayBackend::Overlay && !overlayAvailable) {
			status.backend = DisplayBackend::ImageProvider;
			status.activeMode = ViewMode::Offline;
		}
		status.needSnapshot = (status.backend == DisplayBackend::Overlay);
		status.needQImage = (status.backend == DisplayBackend::ImageProvider);
		status.renderBoxes = status.needQImage || status.isSaveResult ();
		status.download2CPU = gpuFlow && (status.renderBoxes || status.needSnapshot);
		return status;
	}
};
