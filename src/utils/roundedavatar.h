#ifndef ROUNDED_AVATAR_H
#define ROUNDED_AVATAR_H

#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QRectF>
#include <QtGlobal>

namespace RoundedAvatar
{
    inline QImage roundImage(const QImage &source, qreal radiusFraction)
    {
        if (source.isNull())
            return source;

        const qreal shortSide = qMin(source.width(), source.height());
        const qreal maximumRadius = shortSide / 2.0;
        const qreal minimumRadius = qMin(2.0, maximumRadius);
        const qreal radius = qMin(maximumRadius,
            qMax(minimumRadius, shortSide * radiusFraction));

        QImage rounded(source.size(), QImage::Format_ARGB32_Premultiplied);
        rounded.fill(Qt::transparent);

        QPainter painter(&rounded);
        painter.setRenderHint(QPainter::Antialiasing, true);
        QPainterPath clipPath;
        clipPath.addRoundedRect(QRectF(0.0, 0.0, source.width(), source.height()), radius, radius);
        painter.setClipPath(clipPath);
        painter.drawImage(QPoint(0, 0), source);
        return rounded;
    }
}

#endif // ROUNDED_AVATAR_H