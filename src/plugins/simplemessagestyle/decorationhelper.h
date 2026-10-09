#ifndef DECORATIONHELPER_H
#define DECORATIONHELPER_H

#include <QtCore>
#include <QTextCursor>
#include <QTextTable>

class DecorationHelper
{
public:
    struct BubbleGeometrySnapshot
    {
        QRectF documentRect;
        bool initialized = false;
    };

    struct Decoration
    {
        QString html;
        int start;
        int end;
    };

    static void createOrUpdateDecoration(QMap<QString, Decoration> &decorationMap, const QString &id, const QString &html, int start, int end);
    static void removeDecoration(QMap<QString, Decoration> &decorationMap, const QString &id);
    static QString renderBubbleHtml(const QString &messageHtml,
        const QMap<QString, Decoration> &decorationMap);
    static QString renderAdjacentHtml(const QMap<QString, Decoration> &decorationMap);
    static QString renderReactionHtml(const QMap<QString, Decoration> &decorationMap);
    static QRect adjacentDecorationRect(const QRect &bubbleRect, const QSize &decorationSize,
        const QSize &viewportSize, bool outgoing);
    static QRect reactionDecorationRect(const QRect &bubbleRect, const QSize &reactionSize,
        const QSize &viewportSize, bool outgoing);
    static QTextCursor replaceSourceMessageWithSpacer(QTextTableCell cell, qreal height);
    static void setSourceSpacerHeight(QTextCursor &cursor, qreal height);
    static QString bubbleMarkerHtml(const QString &marker);
    static QSet<QUrl> imageResourcesFromHtml(const QString &html);
    static qreal overlayHeightForContent(qreal contentHeight, qreal verticalInsets);
    static bool initializeBubbleGeometry(BubbleGeometrySnapshot &snapshot,
        const QRectF &tableRect, qreal contentHeight, qreal verticalInsets);
    static bool updateImageFormatForResource(QTextDocument &document, const QUrl &resourceUrl,
        const QSize &sourceImageSize, int availableWidth);
    static qreal maximumImageDisplayWidth(const QTextDocument &document);
    static QRectF bubbleRectForContentWidth(const QRectF &currentRect, qreal contentWidth,
        qreal horizontalInsets, bool outgoing);

private:
    DecorationHelper() = default;
};

#endif // DECORATIONHELPER_H