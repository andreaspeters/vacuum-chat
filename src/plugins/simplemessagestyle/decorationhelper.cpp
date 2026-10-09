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

bool DecorationHelper::updateImageFormatForResource(QTextDocument &document,
    const QUrl &resourceUrl, const QSize &sourceImageSize, int availableWidth)
{
    if (resourceUrl.isEmpty() || sourceImageSize.width() <= 0 || sourceImageSize.height() <= 0)
        return false;

    const bool matrixImage = resourceUrl.scheme() == QStringLiteral("vacuum-matrix-image");
    const int widthLimit = matrixImage ? 400 :
        (availableWidth > 0 ? qMin(300, qMax(1, availableWidth / 2)) : 300);
    const qreal scale = matrixImage
        ? qreal(widthLimit) / sourceImageSize.width()
        : qMin(qreal(1.0), qreal(widthLimit) / sourceImageSize.width());
    const int displayHeight = qMax(1, qRound(sourceImageSize.height() * scale));
    const qreal aspectRatio = qreal(sourceImageSize.width()) / sourceImageSize.height();
    const int displayWidth = matrixImage ? widthLimit : qMax(1,
        qMin(widthLimit, qRound(displayHeight * aspectRatio)));

    struct ImageFormatUpdate
    {
        int position;
        int length;
        QTextImageFormat format;
    };
    QList<ImageFormatUpdate> updates;
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next())
    {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it)
        {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid() || !fragment.charFormat().isImageFormat())
                continue;
            QTextImageFormat format = fragment.charFormat().toImageFormat();
            if (QUrl(format.name()) != resourceUrl)
                continue;
            const bool hasDisplaySize = format.width() > 0.0 && format.height() > 0.0;
            const qreal currentAspectRatio = hasDisplaySize ? format.width() / format.height() : 0.0;
            const bool aspectRatioChanged = hasDisplaySize &&
                qAbs(currentAspectRatio - aspectRatio) > qMax(qreal(0.01), aspectRatio * qreal(0.01));
            const bool exceedsBounds = hasDisplaySize &&
                (format.width() > widthLimit || format.width() > sourceImageSize.width() ||
                 format.height() > sourceImageSize.height());
            const bool fixedWidthChanged = matrixImage &&
                !qFuzzyCompare(format.width(), qreal(displayWidth));
            if (hasDisplaySize && !aspectRatioChanged && !exceedsBounds && !fixedWidthChanged)
                continue;
            if (qFuzzyCompare(format.width(), qreal(displayWidth)) &&
                qFuzzyCompare(format.height(), qreal(displayHeight)))
                continue;
            format.setWidth(displayWidth);
            format.setHeight(displayHeight);
            updates.append({fragment.position(), fragment.length(), format});
        }
    }

    if (updates.isEmpty())
        return false;

    QTextCursor editCursor(&document);
    editCursor.beginEditBlock();
    for (const ImageFormatUpdate &update : updates)
    {
        QTextCursor cursor(&document);
        cursor.setPosition(update.position);
        cursor.setPosition(update.position + update.length, QTextCursor::KeepAnchor);
        cursor.setCharFormat(update.format);
    }
    editCursor.endEditBlock();
    return true;
}

qreal DecorationHelper::maximumImageDisplayWidth(const QTextDocument &document)
{
    qreal maximumWidth = 0.0;
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next())
    {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it)
        {
            const QTextFragment fragment = it.fragment();
            if (fragment.isValid() && fragment.charFormat().isImageFormat())
                maximumWidth = qMax(maximumWidth,
                    fragment.charFormat().toImageFormat().width());
        }
    }
    return maximumWidth;
}

QRectF DecorationHelper::bubbleRectForContentWidth(const QRectF &currentRect,
    qreal contentWidth, qreal horizontalInsets, bool outgoing)
{
    if (!currentRect.isValid() || contentWidth <= 0.0)
        return currentRect;

    const qreal targetWidth = qMax(currentRect.width(),
        contentWidth + qMax(qreal(0.0), horizontalInsets));
    if (qFuzzyCompare(targetWidth, currentRect.width()))
        return currentRect;

    QRectF result = currentRect;
    if (outgoing)
        result.setLeft(currentRect.right() - targetWidth);
    else
        result.setWidth(targetWidth);
    return result;
}

qreal DecorationHelper::overlayHeightForContent(qreal contentHeight, qreal verticalInsets)
{
    return qMax(qreal(0.0), contentHeight) + qMax(qreal(0.0), verticalInsets);
}

bool DecorationHelper::initializeBubbleGeometry(BubbleGeometrySnapshot &snapshot,
    const QRectF &tableRect, qreal contentHeight, qreal verticalInsets)
{
	if (tableRect.width() <= 0.0)
		return false;

	if (snapshot.initialized)
	{
		const qreal width = snapshot.documentRect.width();
		snapshot.documentRect.moveTopLeft(tableRect.topLeft());
		snapshot.documentRect.setWidth(width);
	}
	else
	{
		snapshot.documentRect = tableRect;
		snapshot.initialized = true;
	}
	snapshot.documentRect.setHeight(overlayHeightForContent(contentHeight, verticalInsets));
	return true;
}

