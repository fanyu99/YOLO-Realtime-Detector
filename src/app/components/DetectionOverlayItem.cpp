#include "DetectionOverlayItem.h"

#include <QColor>
#include <QImage>
#include <QQuickWindow>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGSimpleTextureNode>
#include <QSGVertexColorMaterial>

#include "CpuFrameHandle.h"
#include "ClassPalette.h"
#include "DetectionApp.h"
#include "Logger.h"

namespace {
	// 4 * 2
	constexpr int VerticesPerBox = 8;
}

DetectionOverlayItem::DetectionOverlayItem ( QQuickItem* parent )
	: QQuickItem ( parent ) {
	setFlag ( QQuickItem::ItemHasContents, true );
	// 叠加层盖住整个画面区，不接管鼠标，避免挡住下层的交互
	setAcceptedMouseButtons ( Qt::NoButton );
}

DetectionOverlayItem::~DetectionOverlayItem () {
	if (auto* app = DetectionApp::instance ())
		app->uninstallOverlay ( this );
}

// QML 组件构造完成（GUI 线程）
void DetectionOverlayItem::componentComplete () {
	QQuickItem::componentComplete ();
	if (auto* app = DetectionApp::instance ())
		app->installOverlay ( this );
}

// GUI 线程
void DetectionOverlayItem::setSnapshot ( FrameSnapshotPtr snapshot ) {
	if (!snapshot || !snapshot->frame)
		return;

	// M1 的纹理来源是CPU上传，要求快照带的是 CPU 句柄
	if (snapshot->frame->memoryType () != MemoryType::CPU && !m_warnedNonCpuFrame) {
		m_warnedNonCpuFrame = true;
		Logger::instance ()->warning ( QStringLiteral (
			"[DetectionOverlayItem]: 快照携带的是 GPU 帧句柄，M1 的上传型纹理源用不了；"
			"请在上游先把帧下载为 CPU 帧（CpuFrameHandle）再投递" ) );
	}

	const int count = static_cast<int>(snapshot->boxes.size ());
	if (count != m_objectCount || !m_frameReady) {
		m_objectCount = count;
		m_frameReady = true;
		emit snapshotChanged ();
	}

	{
		std::lock_guard<std::mutex> lock ( m_mutex );
		m_pending = std::move ( snapshot );
	}
	update ();
}

// 重置帧缓冲（切换离线模式时调用）
void DetectionOverlayItem::resetFrames () {
	m_frameReady = false;
	emit snapshotChanged (); // 补发一次信号刷新
	{
		std::lock_guard<std::mutex> lock ( m_mutex );
		std::exchange ( m_last, nullptr );
		std::exchange ( m_pending, nullptr );
	}
}

DetectionOverlayItem::Nodes DetectionOverlayItem::createNodes () {
	Nodes nodes;
	nodes.root = new QSGNode;

	nodes.pixels = new QSGSimpleTextureNode;
	nodes.pixels->setFlag ( QSGNode::OwnedByParent, true );
	nodes.pixels->setOwnsTexture ( true );
	nodes.pixels->setFiltering ( QSGTexture::Linear );
	nodes.root->appendChildNode ( nodes.pixels );

	nodes.boxes = new QSGGeometryNode;
	nodes.boxes->setFlag ( QSGNode::OwnedByParent, true );
	auto* geometry = new QSGGeometry ( QSGGeometry::defaultAttributes_ColoredPoint2D (), 0 );
	geometry->setDrawingMode ( QSGGeometry::DrawLines );
	nodes.boxes->setGeometry ( geometry );
	nodes.boxes->setMaterial ( new QSGVertexColorMaterial );
	nodes.boxes->setFlags ( QSGNode::OwnsGeometry | QSGNode::OwnsMaterial );
	nodes.root->appendChildNode ( nodes.boxes );

	return nodes;
}

// 渲染线程进行更新节点
QSGNode* DetectionOverlayItem::updatePaintNode ( QSGNode* oldNode, UpdatePaintNodeData* ) {
	FrameSnapshotPtr temp;	 // 渲染线程中使用临时的变量指针获取帧的引用进行渲染，防止锁外访问m_last但是其他线程对m_last进行free导致访问野指针
	{
		std::lock_guard<std::mutex> lock ( m_mutex );
		if (m_pending)
		{
			m_last = std::move ( m_pending );	// m_last从pending移动过来，不持有单独的引用计数
			temp = m_last;
		} 
	}
	if (!temp)
		return oldNode;

	Nodes nodes;
	if (!oldNode) {
		nodes = createNodes ();
		oldNode = nodes.root;
	}
	else {
		nodes.root = oldNode;
		nodes.pixels = static_cast<QSGSimpleTextureNode*>(oldNode->firstChild ());
		nodes.boxes = static_cast<QSGGeometryNode*>(oldNode->lastChild ());
	}

	const FrameHandlePtr& frame = temp->frame;
	const QSizeF frameSize ( frame->width (), frame->height () );
	const QRectF target = fittedRect ( frameSize );
	if (target.isEmpty ())
		return oldNode;
	// 像素
	nodes.pixels->setTexture ( textureFor ( frame ) );
	nodes.pixels->setRect ( target );

	// 几何
	const qreal scale = target.width () / frameSize.width ();
	syncBoxes ( nodes, target, scale, temp->boxes );

	return oldNode;
}

QSGTexture* DetectionOverlayItem::textureFor ( const FrameHandlePtr& frame ) {
	if (!frame || frame->memoryType () != MemoryType::CPU)
		return nullptr;

	// 零拷贝取引用：QImage 只是包一层，不复制像素
	const cv::Mat mat = toCvMat ( frame );
	if (mat.empty () || mat.type () != CV_8UC3)
		return nullptr;

	const QImage view ( mat.data, mat.cols, mat.rows, static_cast<int>(mat.step),
		QImage::Format_BGR888 );
	return window ()->createTextureFromImage ( view, QQuickWindow::TextureIsOpaque );
}

QRectF DetectionOverlayItem::fittedRect ( const QSizeF& frameSize ) const {
	const QRectF area = boundingRect ();
	if (frameSize.isEmpty () || area.isEmpty ())
		return QRectF ();

	const QSizeF scaled = frameSize.scaled ( area.size (), Qt::KeepAspectRatio );
	QRectF target ( QPointF ( 0, 0 ), scaled );
	target.moveCenter ( area.center () );
	return target;
}

void DetectionOverlayItem::syncBoxes ( const Nodes& nodes, const QRectF& target, qreal scale,
	const QVector<Box>& boxes ) {
	QSGGeometry* geometry = nodes.boxes->geometry ();
	const int needed = VerticesPerBox * static_cast<int>(boxes.size ());

	// allocate 只增不减（避免逐帧重新分配），这一帧实际画多少由 setVertexCount 决定，
	// 没有框时置 0 就等于不画
	if (geometry->vertexCount () < needed)
		geometry->allocate ( needed );
	geometry->setVertexCount ( needed );

	if (needed > 0) {
		QSGGeometry::ColoredPoint2D* vertex = geometry->vertexDataAsColoredPoint2D ();
		for (const Box& box : boxes) {
			// Box 是原图像素坐标的中心点 + 宽高，映射到 item 坐标
			const QRectF rect ( (box.x - box.w * 0.5) * scale + target.x (),
				(box.y - box.h * 0.5) * scale + target.y (),
				box.w * scale, box.h * scale );

			const QColor color = ClassPalette::getClassColor ( box.cls );
			const uchar r = static_cast<uchar>(color.red ());
			const uchar g = static_cast<uchar>(color.green ());
			const uchar b = static_cast<uchar>(color.blue ());
			const float left = static_cast<float>(rect.left ());
			const float top = static_cast<float>(rect.top ());
			const float right = static_cast<float>(rect.right ());
			const float bottom = static_cast<float>(rect.bottom ());

			// 四条边：上、右、下、左
			vertex[0].set ( left, top, r, g, b, 255 );
			vertex[1].set ( right, top, r, g, b, 255 );
			vertex[2].set ( right, top, r, g, b, 255 );
			vertex[3].set ( right, bottom, r, g, b, 255 );
			vertex[4].set ( right, bottom, r, g, b, 255 );
			vertex[5].set ( left, bottom, r, g, b, 255 );
			vertex[6].set ( left, bottom, r, g, b, 255 );
			vertex[7].set ( left, top, r, g, b, 255 );
			vertex += VerticesPerBox;
		}
	}

	nodes.boxes->markDirty ( QSGNode::DirtyGeometry );
}
