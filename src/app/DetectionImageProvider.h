#pragma once
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QQueue>
#include <QQuickImageProvider>
#include <QString>

// 帧提供器：既服务"实时画面"，也服务"结果回看"。
//
//   image://detection/latest?rev=N     实时最新帧（rev 变化才重新取图）
//   image://detection/frame/<seq>      回看用的某一帧（seq 由 appendResultFrame 返回）
//
// 回看缓冲是**有界的环形**。关键设计：推理线程不会因为 UI 看得慢而被阻塞，
// UI 只按自己的节奏推进索引 —— 两边彻底解耦，也就不需要任何跨线程的"等待/放行"标志位。
//
// requestImage 可能被场景图渲染线程调用（不一定是 GUI 线程），
// 所以写帧和取帧都要加锁。帧数据本身在生成后不再修改，共享是安全的。
class DetectionImageProvider : public QQuickImageProvider {
public:
	DetectionImageProvider();

	// 回看缓冲上限。推理线程只负责追加，不会因为 UI 看得慢而被阻塞。
	static constexpr int kMaxReviewFrames = 60;

	static QString providerId();					// "detection"
	static QString sourceUrl(int revision);			// 实时帧的 image:// URL
	static QString resultUrl(quint64 seq);			// 回看帧的 image:// URL

	void setFrame(const QImage& frame);				// 线程安全
	QImage frame() const;							// 线程安全

	// 追加一帧到回看缓冲，返回它的序号；超出上限时淘汰最旧的一帧
	quint64 appendResultFrame(const QImage& frame);
	void clearResultFrames();
	int resultFrameCount() const;

	QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

private:
	// 回看副本按需缩小：几十帧原分辨率画面会把内存吃掉
	// （1920x1080 x 60 帧约 370MB，缩到 1280 宽后约 165MB）
	static QImage scaledForReview(const QImage& frame);

	mutable QMutex m_mutex;
	QImage m_frame;

	// 回看缓冲：序号 -> 帧，另用队列记淘汰顺序
	QHash<quint64, QImage> m_results;
	QQueue<quint64> m_resultOrder;
	quint64 m_nextResultSeq = 1;
};
