#include "RtspImageProvider.h"
#include <QMutexLocker>


RtspImageProvider::RtspImageProvider()
	: QQuickImageProvider( QQuickImageProvider::Image )
{
}

QImage RtspImageProvider::requestImage( const QString& id, QSize* size, const QSize& requestedSize ) {
	Q_UNUSED( id );

	QImage frame;
	{
		QMutexLocker locker( &m_mutex );
		frame = m_latestFrame; // QImage 隐式共享，仅拷贝句柄
	}

	if (frame.isNull())
		return QImage{};

	// 先把原始尺寸告诉 QML，再按 requestedSize 等比缩放
	if (size)
		*size = frame.size();

	if (requestedSize.isValid() && requestedSize != frame.size()) {
		frame = frame.scaled( requestedSize, Qt::KeepAspectRatio, Qt::FastTransformation );
	}

	return frame;
}

void RtspImageProvider::updateFrame( const LastestFrameStore& frame ) {
	if (!frame.isValid())
		return;
	updateFrame( frame.lastestFrame );
}

void RtspImageProvider::updateFrame( const QImage& frame ) {
	if (frame.isNull())
		return;
	QMutexLocker locker( &m_mutex );
	m_latestFrame = frame;
	++m_frameIndex;
}

void RtspImageProvider::clear() {
	QMutexLocker locker( &m_mutex );
	m_latestFrame = QImage{};
	m_frameIndex = 0;
}

bool RtspImageProvider::hasFrame() const {
	QMutexLocker locker( &m_mutex );
	return !m_latestFrame.isNull();
}

quint64 RtspImageProvider::frameIndex() const {
	QMutexLocker locker( &m_mutex );
	return m_frameIndex;
}
