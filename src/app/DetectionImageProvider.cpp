#include "DetectionImageProvider.h"

namespace {
	constexpr int kReviewMaxWidth = 1280;
	constexpr auto kResultPrefix = "frame/";
}

DetectionImageProvider::DetectionImageProvider()
	: QQuickImageProvider(QQuickImageProvider::Image) {}

QString DetectionImageProvider::providerId() {
	return QStringLiteral("detection");
}

QString DetectionImageProvider::sourceUrl(int revision) {
	return QStringLiteral("image://") + providerId()
		+ QStringLiteral("/latest?rev=") + QString::number(revision);
}

QString DetectionImageProvider::resultUrl(quint64 seq) {
	return QStringLiteral("image://") + providerId()
		+ QStringLiteral("/frame/") + QString::number(seq);
}

void DetectionImageProvider::setFrame(const QImage& frame) {
	QMutexLocker locker(&m_mutex);
	m_frame = frame;			// QImage 隐式共享，这里只是换一份引用，不拷贝像素
}

QImage DetectionImageProvider::frame() const {
	QMutexLocker locker(&m_mutex);
	return m_frame;
}

QImage DetectionImageProvider::scaledForReview(const QImage& frame) {
	if (frame.width() <= kReviewMaxWidth)
		return frame;
	return frame.scaledToWidth(kReviewMaxWidth, Qt::FastTransformation);
}

quint64 DetectionImageProvider::appendResultFrame(const QImage& frame) {
	if (frame.isNull())
		return 0;

	QMutexLocker locker(&m_mutex);

	const quint64 seq = m_nextResultSeq++;
	m_results.insert(seq, scaledForReview(frame));
	m_resultOrder.enqueue(seq);

	// 环形淘汰：超出上限就把最旧的丢掉
	while (m_resultOrder.size() > kMaxReviewFrames)
		m_results.remove(m_resultOrder.dequeue());

	return seq;
}

void DetectionImageProvider::clearResultFrames() {
	QMutexLocker locker(&m_mutex);
	m_results.clear();
	m_resultOrder.clear();
}

int DetectionImageProvider::resultFrameCount() const {
	QMutexLocker locker(&m_mutex);
	return m_results.size();
}

QImage DetectionImageProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize) {
	QImage source;

	{
		QMutexLocker locker(&m_mutex);

		if (id.startsWith(QLatin1String(kResultPrefix))) {
			bool ok = false;
			const quint64 seq = id.mid(static_cast<int>(qstrlen(kResultPrefix))).toULongLong(&ok);
			if (!ok || !m_results.contains(seq)) {
				if (size)
					*size = QSize();
				return QImage();
			}
			source = m_results.value(seq);
		}
		else {
			source = m_frame;	// "latest"
		}
	}

	if (source.isNull()) {
		if (size)
			*size = QSize();
		return QImage();
	}

	if (size)
		*size = source.size();

	// QML 若指定了目标尺寸就缩放，否则原样给
	if (requestedSize.isValid() && requestedSize != source.size())
		return source.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

	return source;
}
