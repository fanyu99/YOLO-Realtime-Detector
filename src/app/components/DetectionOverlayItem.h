#pragma once
#include <QQuickItem>
#include <memory>
#include <mutex>

#include "Box.h"
#include "FrameSnapshot.h"
#include "OverlayTextureSource.h"

class QSGNode;
class QSGGeometryNode;
class QSGSimpleTextureNode;
class QSGTexture;

// 检测框叠加层：将检测框叠加在画面上
// 仅在UI线程中调用
class DetectionOverlayItem : public QQuickItem {
	Q_OBJECT
	QML_ELEMENT
	Q_PROPERTY(int objectCount READ objectCount NOTIFY snapshotChanged)
	Q_PROPERTY(bool frameReady READ frameReady NOTIFY snapshotChanged)

public:
	explicit DetectionOverlayItem(QQuickItem* parent = nullptr);
	~DetectionOverlayItem() override;

	// 设置快照到pending
	void setSnapshot(FrameSnapshotPtr snapshot);

	int objectCount() const { return m_objectCount; }
	// 是否已经拿到过帧,QML判断使用overlay显示还是ImageProvider
	bool frameReady() const { return m_frameReady; }

	// 注册到 App（App 从这里拿指针把快照推过来）
	void componentComplete() override;

	QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) override;

	// 重置帧，在切换离线模式时重置
	Q_INVOKABLE void resetFrames ();

	// 判断是否互操作可用
	bool nativeInteropAvailable () const { return m_textureSource && m_textureSource->nativeInteropAvailable (); }

signals:
	void snapshotChanged();
	void nativeChanged (bool ok);

private:
	FrameTextureSourcePtr m_textureSource{nullptr};	// 纹理源，根据帧句柄内存的类型选择上传或互操作
	// 图数据节点（像素块+检测框）
	// 后续复用节点
	struct Nodes {
		QSGNode* root = nullptr;
		QSGSimpleTextureNode* pixels = nullptr;	// 帧像素，画在下面
		QSGGeometryNode* boxes = nullptr;		// 检测框，画在上面
	};
	static Nodes createNodes();

	// 从帧句柄获取纹理，CPU上传，GPU直接转换
	QSGTexture* textureFor(const FrameHandlePtr& frame);

	// 帧矩形
	QRectF fittedRect(const QSizeF& frameSize) const;

	// 把检测框（原图像素坐标）写进顶点缓冲
	void syncBoxes(const Nodes& nodes, const QRectF& target, qreal scale, const QVector<Box>& boxes);

	mutable std::mutex m_mutex;
	FrameSnapshotPtr m_pending;		// GUI 线程写
	FrameSnapshotPtr m_last;		// 当前显示的帧，渲染线程
	int m_objectCount = 0;			// 最近一次推入的框数
	bool m_frameReady = false;		// 是否已经拿到过帧（GUI 线程读写）
};
