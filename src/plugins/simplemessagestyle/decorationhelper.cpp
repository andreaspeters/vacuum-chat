#include "decorationhelper.h"

#include <QTextBlockFormat>
#include <QTextCursor>

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
        if (it.key() != QStringLiteral("protocol-message-status") &&
            it.key() != QStringLiteral("protocol-message-reactions"))
            rendered += it.value().html;
    }
    return rendered;
}

QString DecorationHelper::renderAdjacentHtml(const QMap<QString, Decoration> &decorationMap)
{
    return decorationMap.value(QStringLiteral("protocol-message-status")).html;
}

QString DecorationHelper::renderReactionHtml(const QMap<QString, Decoration> &decorationMap)
{
    return decorationMap.value(QStringLiteral("protocol-message-reactions")).html;
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

QRect DecorationHelper::reactionDecorationRect(const QRect &bubbleRect, const QSize &reactionSize,
    const QSize &viewportSize, bool outgoing)
{
    if (!bubbleRect.isValid() || reactionSize.width() <= 0 || reactionSize.height() <= 0 ||
        viewportSize.width() <= 0)
        return QRect();

    const int requestedX = outgoing
        ? bubbleRect.right() - reactionSize.width() + 1
        : bubbleRect.left();
    const int x = qBound(0, requestedX, qMax(0, viewportSize.width() - reactionSize.width()));
    const int y = bubbleRect.bottom() + 3;
    return QRect(x, y, reactionSize.width(), reactionSize.height());
}

QTextCursor DecorationHelper::replaceSourceMessageWithSpacer(QTextTableCell cell, qreal height)
{
    if (!cell.isValid())
        return QTextCursor();

    QTextCursor contentCursor = cell.firstCursorPosition();
    contentCursor.setPosition(cell.lastCursorPosition().position(), QTextCursor::KeepAnchor);
    contentCursor.removeSelectedText();

    QTextTableCellFormat cellFormat = cell.format().toTableCellFormat();
    cellFormat.setPadding(0.0);
    cell.setFormat(cellFormat);

    QTextCursor spacerCursor = cell.firstCursorPosition();
    setSourceSpacerHeight(spacerCursor, height);
    return spacerCursor;
}

void DecorationHelper::setSourceSpacerHeight(QTextCursor &cursor, qreal height)
{
    if (cursor.isNull())
        return;

    QTextBlockFormat blockFormat = cursor.blockFormat();
    blockFormat.setTopMargin(0.0);
    blockFormat.setBottomMargin(0.0);
    blockFormat.setLeftMargin(0.0);
    blockFormat.setRightMargin(0.0);
    blockFormat.setLineHeight(qMax(1, qRound(height)), QTextBlockFormat::FixedHeight);
    cursor.setBlockFormat(blockFormat);
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

