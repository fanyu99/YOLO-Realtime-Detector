#pragma once
#include <opencv2/opencv.hpp>
#include <QElapsedTimer>
#include "Box.h"
#include "FrameHandle.h"
#include	 <QVector>
#include <memory>
// 快照，存有像素数据和几何数据、序列号、发送时间
struct FrameSnapshot {
	FrameHandlePtr frame; // 帧数据
	QVector<Box> boxes; // 检测框列表
	quint64 seq = 0; // 帧序号
	quint64 frameStartNs = 0; // 读帧开始时间，计算后面的延迟
	quint64 postTime = 0; // 开始投递到邮箱时间
	FrameSnapshot () = default;
	~FrameSnapshot () = default;
	FrameSnapshot(const FrameHandlePtr& f, const QVector<Box>& bxs, quint64 s,
		quint64 startNs, quint64 postTimeNs)
		: frame(f), boxes(bxs), seq(s), frameStartNs(startNs), postTime(postTimeNs) {}
};
using FrameSnapshotPtr = std::shared_ptr<const FrameSnapshot>;

// 单调时钟，开始-结束，获取差值
// 获取从投递邮箱开始到显示（Overlay等）的时间
inline quint64 frameClockNs () {
	static QElapsedTimer clock = [] { QElapsedTimer t; t.start (); return t; } ();
	return static_cast<quint64>(clock.nsecsElapsed ());
}