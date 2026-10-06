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

        QImage sourcePixels = source;
        sourcePixels.setDevicePixelRatio(1.0);

        const qreal shortSide = qMin(sourcePixels.width(), sourcePixels.height());
        const qreal maximumRadius = shortSide / 2.0;
        const qreal minimumRadius = qMin(2.0, maximumRadius);
        const qreal radius = qMin(maximumRadius,
            qMax(minimumRadius, shortSide * radiusFraction));

        QImage rounded(sourcePixels.size(), QImage::Format_ARGB32_Premultiplied);
        rounded.fill(Qt::transparent);

        QPainter painter(&rounded);
        painter.setRenderHint(QPainter::Antialiasing, true);
        QPainterPath clipPath;
        clipPath.addRoundedRect(QRectF(0.0, 0.0, source.width(), source.height()), radius, radius);
        painter.setClipPath(clipPath);
        painter.drawImage(QPoint(0, 0), sourcePixels);
        return rounded;
    }

    inline QImage roundImageScaled(const QImage &source, const QSize &targetSize,
        qreal radiusFraction = 0.18)
    {
        if (source.isNull() || !targetSize.isValid())
            return source;

        const QImage scaled = source.size() == targetSize
            ? source : source.scaled(targetSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        return roundImage(scaled, radiusFraction);
    }
}

#endif // ROUNDED_AVATAR_H