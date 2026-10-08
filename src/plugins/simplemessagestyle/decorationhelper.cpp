#include "decorationhelper.h"

#include <QBrush>
#include <QTextCharFormat>
#include <QTextCursor>

void DecorationHelper::hideSourceMessageText(QTextCursor &cursor)
{
    if (!cursor.hasSelection())
        return;

    QTextCharFormat hiddenTextFormat;
    hiddenTextFormat.setForeground(QBrush(Qt::transparent));
    cursor.mergeCharFormat(hiddenTextFormat);
}

void DecorationHelper::createOrUpdateDecoration(QMap<QString, Decoration> &decorationMap, const QString &id, const QString &html, int start, int end)
{
    if (html.isEmpty())
    {
        removeDecoration(decorationMap, id);
        return;
    }

    decorationMap[id] = {html, start, end};
}

void DecorationHelper::removeDecoration(QMap<QString, Decoration> &decorationMap, const QString &id)
{
    decorationMap.remove(id);
}

QString DecorationHelper::renderBubbleHtml(const QString &messageHtml,
    const QMap<QString, Decoration> &decorationMap)
{
    QString rendered = messageHtml;
    for (auto it = decorationMap.constBegin(); it != decorationMap.constEnd(); ++it)
    {
        if (it.key() != QStringLiteral("protocol-message-status"))
            rendered += it.value().html;
    }
    return rendered;
}

QString DecorationHelper::renderAdjacentHtml(const QMap<QString, Decoration> &decorationMap)
{
    return decorationMap.value(QStringLiteral("protocol-message-status")).html;
}

QRect DecorationHelper::adjacentDecorationRect(const QRect &bubbleRect,
    const QSize &decorationSize, const QSize &viewportSize, bool outgoing)
{
    if (!bubbleRect.isValid() || decorationSize.width() <= 0 || decorationSize.height() <= 0)
        return QRect();

    const int gap = 4;
    const int left = bubbleRect.left() - decorationSize.width() - gap;
    const int right = bubbleRect.right() + gap + 1;
    const bool leftFits = left >= 0;
    const bool rightFits = right + decorationSize.width() <= viewportSize.width();
    if (!leftFits && !rightFits)
        return QRect();

    const int x = outgoing
    	? (rightFits ? right : left)
    	: (leftFits ? left : right);
    const int y = qMax(0, bubbleRect.bottom() - decorationSize.height() + 1);
    return QRect(x, y, decorationSize.width(), decorationSize.height());
}

QString DecorationHelper::bubbleMarkerHtml(const QString &marker)
{
    return QStringLiteral("<span style=\"font-size:1px; line-height:1px; color:transparent;\">%1</span>")
        .arg(marker.toHtmlEscaped());
}

QSet<QUrl> DecorationHelper::imageResourcesFromHtml(const QString &html)
{
    static const QRegularExpression imageSourceExpression(
        QStringLiteral("<img\\b[^>]*\\bsrc\\s*=\\s*(?:\"([^\"]*)\"|'([^']*)')"),
        QRegularExpression::CaseInsensitiveOption);
    QSet<QUrl> imageResources;
    QRegularExpressionMatchIterator matches = imageSourceExpression.globalMatch(html);
    while (matches.hasNext())
    {
        const QRegularExpressionMatch match = matches.next();
        const QString source = match.captured(1).isEmpty() ? match.captured(2) : match.captured(1);
        if (!source.isEmpty())
            imageResources.insert(QUrl::fromEncoded(source.toUtf8()));
    }
    return imageResources;
}

qreal DecorationHelper::overlayHeightForContent(qreal contentHeight, qreal verticalInsets)
{
	return qMax(qreal(0.0), contentHeight) + qMax(qreal(0.0), verticalInsets);
}

bool DecorationHelper::initializeBubbleGeometry(BubbleGeometrySnapshot &snapshot,
    const QRectF &tableRect, qreal contentHeight, qreal verticalInsets)
{
    if (snapshot.initialized || tableRect.width() <= 0.0)
        return false;

    snapshot.documentRect = tableRect;
    snapshot.documentRect.setHeight(overlayHeightForContent(contentHeight, verticalInsets));
    snapshot.initialized = true;
    return true;
}

