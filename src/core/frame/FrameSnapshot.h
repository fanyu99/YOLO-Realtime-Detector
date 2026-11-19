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
	quint64 postTime = 0; // 开始投递到邮箱时间
	FrameSnapshot () = default;
	~FrameSnapshot () = default;
	FrameSnapshot(const FrameHandlePtr& f, const QVector<Box>& bxs, quint64 s, quint64 t)
		: frame(f), boxes(bxs), seq(s), postTime(t) {}
};
using FrameSnapshotPtr = std::shared_ptr<const FrameSnapshot>;

// postTime 用的单调时钟（ns）。生产者填时间、消费者算差值（显示等待耗时），
// 两边必须共用同一基准，所以放在快照定义旁边，而不是各自的匿名命名空间里。
inline quint64 frameClockNs () {
	static QElapsedTimer clock = [] { QElapsedTimer t; t.start (); return t; } ();
	return static_cast<quint64>(clock.nsecsElapsed ());
}