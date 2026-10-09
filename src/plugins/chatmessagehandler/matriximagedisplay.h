#ifndef MATRIXIMAGEDISPLAY_H
#define MATRIXIMAGEDISPLAY_H

#include <QSize>
#include <QtGlobal>

namespace MatrixImageDisplay
{
inline QSize sizeForDisplay(const QSize &sourceSize, int availableWidth)
{
	Q_UNUSED(availableWidth);
	if (sourceSize.width() <= 0 || sourceSize.height() <= 0)
		return QSize();

	constexpr int displayWidth = 400;
	const int displayHeight = qMax(1, qRound(
		qreal(sourceSize.height()) * displayWidth / sourceSize.width()));
	return QSize(displayWidth, displayHeight);
}
}

#endif // MATRIXIMAGEDISPLAY_H
