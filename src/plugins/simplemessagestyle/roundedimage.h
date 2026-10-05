#ifndef ROUNDEDIMAGE_H
#define ROUNDEDIMAGE_H

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QRectF>
#include <QtGlobal>

namespace RoundedImage
{
inline QImage withRoundedCorners(const QImage &source, qreal radiusFraction,
	const QColor &borderColor = QColor())
{
	if (source.isNull())
		return source;

	const qreal shortSide = qMin(source.width(), source.height());
	const qreal maximumRadius = shortSide / 2.0;
	const qreal radius = qMin(maximumRadius,
		qMax(qMin(qreal(2.0), maximumRadius), shortSide * radiusFraction));
	QImage rounded(source.size(), QImage::Format_ARGB32_Premultiplied);
	rounded.fill(Qt::transparent);

	QPainter painter(&rounded);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
	QPainterPath clipPath;
	clipPath.addRoundedRect(QRectF(0.0, 0.0, source.width(), source.height()), radius, radius);
	painter.setClipPath(clipPath);
	painter.drawImage(QPointF(0.0, 0.0), source);
	painter.setClipping(false);

	if (borderColor.isValid())
	{
		QPainterPath borderPath;
		borderPath.addRoundedRect(QRectF(0.5, 0.5, source.width() - 1.0, source.height() - 1.0),
			radius, radius);
		painter.setPen(QPen(borderColor, 1.0));
		painter.setBrush(Qt::NoBrush);
		painter.drawPath(borderPath);
	}

	return rounded;
}

}

#endif // ROUNDEDIMAGE_H
