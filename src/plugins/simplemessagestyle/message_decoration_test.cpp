#include "decorationhelper.h"
#include "../../interfaces/messagedecorationpolicy.h"

#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextImageFormat>
#include <QTextTable>
#include <cstdio>

namespace
{
QTextImageFormat findImageFormat(const QTextDocument &document, const QUrl &resourceUrl)
{
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next())
    {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it)
        {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid() || !fragment.charFormat().isImageFormat())
                continue;
            const QTextImageFormat format = fragment.charFormat().toImageFormat();
            if (QUrl(format.name()) == resourceUrl)
                return format;
        }
    }
    return QTextImageFormat();
}
bool check(bool condition, const char *description)
{
    if (!condition)
        std::fprintf(stderr, "FAIL: %s\n", description);
    return condition;
}
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QMap<QString, DecorationHelper::Decoration> decorations;
    DecorationHelper::createOrUpdateDecoration(decorations, QStringLiteral("status"),
        QStringLiteral("<div>sent</div>"), 0, 0);
    bool passed = check(DecorationHelper::renderBubbleHtml(QStringLiteral("<p>message</p>"), decorations) ==
        QStringLiteral("<p>message</p><div>sent</div>"),
        "visible bubble HTML includes a newly added decoration");


    DecorationHelper::createOrUpdateDecoration(decorations, QStringLiteral("status"),
        QStringLiteral("<div>read</div>"), 0, 0);
    passed &= check(DecorationHelper::renderBubbleHtml(QStringLiteral("<p>message</p>"), decorations) ==
        QStringLiteral("<p>message</p><div>read</div>"),
        "updating a decoration replaces its old HTML rather than duplicating it");

    DecorationHelper::createOrUpdateDecoration(decorations, QStringLiteral("status"), QString(), 0, 0);
    passed &= check(DecorationHelper::renderBubbleHtml(QStringLiteral("<p>message</p>"), decorations) ==
        QStringLiteral("<p>message</p>"), "removing a decoration leaves the message body unchanged");

    const QString localAvatar = QStringLiteral("<img src=\"vacuum-avatar:/abc123\" width=\"10\" height=\"10\"/>");
    DecorationHelper::createOrUpdateDecoration(decorations, QStringLiteral("status"), localAvatar, 0, 0);
    passed &= check(DecorationHelper::renderBubbleHtml(QStringLiteral("<p>message</p>"), decorations) ==
        QStringLiteral("<p>message</p>") + localAvatar,
        "decoration rendering preserves local avatar resource markup");
    const QSet<QUrl> imageResources = DecorationHelper::imageResourcesFromHtml(localAvatar);
    passed &= check(imageResources.size() == 1 &&
        imageResources.contains(QUrl::fromEncoded("vacuum-avatar:/abc123")),
        "local avatar URL is included in the overlay resource set");

    const QUrl lateImageUrl(QStringLiteral("vacuum-matrix-image:/room/event"));
    QTextDocument lateImageDocument;
    lateImageDocument.setHtml(QStringLiteral("<img src=\"%1\"/>").arg(lateImageUrl.toString()));
    QImage loadedImage(1600, 900, QImage::Format_ARGB32);
    loadedImage.fill(Qt::red);
    lateImageDocument.addResource(QTextDocument::ImageResource, lateImageUrl, loadedImage);
    lateImageDocument.markContentsDirty(0, lateImageDocument.characterCount());
    const QTextImageFormat initialImageFormat = findImageFormat(lateImageDocument, lateImageUrl);
    passed &= check(initialImageFormat.width() == 0.0 && initialImageFormat.height() == 0.0,
        "late-loaded image starts with no explicit display dimensions");
    const bool resizedLateImage = DecorationHelper::updateImageFormatForResource(
        lateImageDocument, lateImageUrl, QSize(1600, 900), 800);
    const QTextImageFormat resizedImageFormat = findImageFormat(lateImageDocument, lateImageUrl);
    passed &= check(resizedLateImage && resizedImageFormat.width() == 400.0 &&
        resizedImageFormat.height() == 225.0,
        "late Matrix image resource receives the fixed width and aspect-preserving height in its existing document");

    lateImageDocument.setDocumentMargin(0.0);
    lateImageDocument.setTextWidth(293.0);
    QImage renderedDocument(500, 300, QImage::Format_RGB32);
    renderedDocument.fill(Qt::white);
    {
        QPainter painter(&renderedDocument);
        lateImageDocument.drawContents(&painter, QRectF(0.0, 0.0, 500.0, 300.0));
    }
    int firstRedX = renderedDocument.width();
    int lastRedX = -1;
    for (int y = 0; y < renderedDocument.height(); ++y)
    {
        for (int x = 0; x < renderedDocument.width(); ++x)
        {
            const QRgb pixel = renderedDocument.pixel(x, y);
            if (qRed(pixel) > 200 && qGreen(pixel) < 80 && qBlue(pixel) < 80)
            {
                firstRedX = qMin(firstRedX, x);
                lastRedX = qMax(lastRedX, x);
            }
        }
    }
    passed &= check(lastRedX - firstRedX + 1 == 400,
        "Qt paints the Matrix image at 400 px even when document text width is 293 px");
    passed &= check(DecorationHelper::maximumImageDisplayWidth(lateImageDocument) == 400.0,
        "bubble sizing reads the explicit 400 px width from its inline image format");

    const QUrl oldSizeUrl(QStringLiteral("vacuum-matrix-image:/old-size"));
    QTextDocument oldSizeDocument;
    oldSizeDocument.setHtml(QStringLiteral("<img src=\"%1\" width=\"300\" height=\"169\"/>")
        .arg(oldSizeUrl.toString()));
    const bool upgradedOldSize = DecorationHelper::updateImageFormatForResource(
        oldSizeDocument, oldSizeUrl, QSize(1600, 900), 800);
    const QTextImageFormat upgradedFormat = findImageFormat(oldSizeDocument, oldSizeUrl);
    passed &= check(upgradedOldSize && upgradedFormat.width() == 400.0 &&
        upgradedFormat.height() == 225.0,
        "already-rendered Matrix images are upgraded to 400 px after resource arrival");

    const QUrl iconUrl(QStringLiteral("vacuum-avatar:/small"));
    QTextDocument iconDocument;
    iconDocument.setHtml(QStringLiteral("<img src=\"%1\" width=\"16\" height=\"16\"/>")
        .arg(iconUrl.toString()));
    const bool resizedSmallIcon = DecorationHelper::updateImageFormatForResource(
        iconDocument, iconUrl, QSize(64, 64), 800);
    const QTextImageFormat iconFormat = findImageFormat(iconDocument, iconUrl);
    passed &= check(!resizedSmallIcon && iconFormat.width() == 16.0 && iconFormat.height() == 16.0,
        "already-sized small inline images are not enlarged to their source pixel dimensions");
    passed &= check(!DecorationHelper::updateImageFormatForResource(
        iconDocument, iconUrl, QSize(64, 64), 800),
        "repeated resource notifications leave a correctly-sized small non-Matrix image unchanged");

    const QUrl distortedUrl(QStringLiteral("vacuum-matrix-image:/distorted"));
    QTextDocument distortedDocument;
    distortedDocument.setHtml(QStringLiteral("<img src=\"%1\" width=\"200\" height=\"50\"/>")
        .arg(distortedUrl.toString()));
    const bool correctedAspectRatio = DecorationHelper::updateImageFormatForResource(
        distortedDocument, distortedUrl, QSize(1600, 900), 800);
    const QTextImageFormat correctedFormat = findImageFormat(distortedDocument, distortedUrl);
    passed &= check(correctedAspectRatio && correctedFormat.width() == 400.0 &&
        correctedFormat.height() == 225.0,
        "a stretched inline image is corrected to the loaded image aspect ratio");

    passed &= check(DecorationHelper::overlayHeightForContent(18.0, 4.0) == 22.0,
        "bubble height follows visible content plus only explicit compact padding");

    DecorationHelper::BubbleGeometrySnapshot bubbleGeometry;
    const QRectF initialTableRect(12.0, 24.0, 180.0, 140.0);
    const bool initialized = DecorationHelper::initializeBubbleGeometry(
        bubbleGeometry, initialTableRect, 18.0, 10.0);
    const QRectF initialBubbleRect = bubbleGeometry.documentRect;
    const bool resizedAfterImageLoad = DecorationHelper::initializeBubbleGeometry(
        bubbleGeometry, QRectF(12.0, 24.0, 180.0, 500.0), 90.0, 10.0);
    passed &= check(initialized && bubbleGeometry.initialized &&
        initialBubbleRect == QRectF(12.0, 24.0, 180.0, 28.0),
        "new bubble geometry is captured from its first stable content measurement");
    passed &= check(resizedAfterImageLoad &&
        bubbleGeometry.documentRect == QRectF(12.0, 24.0, 180.0, 100.0),
        "late image content grows the existing bubble height");
    const QRectF expandedIncoming = DecorationHelper::bubbleRectForContentWidth(
        bubbleGeometry.documentRect, 400.0, 7.0, false);
    passed &= check(expandedIncoming == QRectF(12.0, 24.0, 407.0, 100.0),
        "incoming bubble expands to show a 400 px image plus its measured frame insets");
    const QRectF expandedOutgoing = DecorationHelper::bubbleRectForContentWidth(
        QRectF(300.0, 24.0, 180.0, 100.0), 400.0, 7.0, true);
    passed &= check(expandedOutgoing == QRectF(73.0, 24.0, 407.0, 100.0),
        "outgoing bubble expands leftward while preserving its right alignment");
    passed &= check(DecorationHelper::bubbleRectForContentWidth(
        bubbleGeometry.documentRect, 16.0, 7.0, false) == bubbleGeometry.documentRect,
        "a small inline image does not shrink or inflate the existing bubble");

    const QString markerHtml = DecorationHelper::bubbleMarkerHtml(QStringLiteral("marker"));
    passed &= check(markerHtml.contains(QStringLiteral("font-size:1px")) &&
        markerHtml.contains(QStringLiteral("line-height:1px")) &&
        !markerHtml.contains(QStringLiteral("font-size:0px")) &&
        !markerHtml.contains(QStringLiteral("line-height:0px")),
        "bubble lookup marker uses a positive font size to avoid Qt font warnings");

    passed &= check(MessageDecorationPolicy::routeForStyleResult(true) ==
        MessageDecorationPolicy::StyleWidgetHandled,
        "style-rendered decorations are not duplicated in the backing document");
    passed &= check(MessageDecorationPolicy::routeForStyleResult(false) ==
        MessageDecorationPolicy::InsertIntoDocument,
        "styles without an overlay retain the backing-document decoration path");

    QMap<QString, DecorationHelper::Decoration> positionedDecorations;
    const QString statusHtml = QStringLiteral("<span class=\"sent\">&#10003;</span>");
    const QString reactionsHtml = QStringLiteral("<span class=\"reaction\">👍 1</span>");
    DecorationHelper::createOrUpdateDecoration(positionedDecorations,
        QStringLiteral("protocol-message-status"), statusHtml, 0, 0);
    DecorationHelper::createOrUpdateDecoration(positionedDecorations,
        QStringLiteral("protocol-message-reactions"), reactionsHtml, 0, 0);
    passed &= check(DecorationHelper::renderBubbleHtml(QStringLiteral("<p>message</p>"),
        positionedDecorations) == QStringLiteral("<p>message</p>"),
        "status and reactions are excluded from bubble content");
    passed &= check(DecorationHelper::renderAdjacentHtml(positionedDecorations) == statusHtml,
        "successful-send status is rendered as an adjacent decoration");
    passed &= check(DecorationHelper::renderReactionHtml(positionedDecorations) == reactionsHtml,
        "reaction chips are rendered separately from bubble and status content");

    const QRect outgoingBubble(150, 100, 80, 40);
    const QRect outgoingStatus = DecorationHelper::adjacentDecorationRect(outgoingBubble,
        QSize(20, 12), QSize(400, 300), true);
    passed &= check(!outgoingStatus.intersects(outgoingBubble) &&
        outgoingStatus.bottom() == outgoingBubble.bottom() &&
        outgoingStatus.left() > outgoingBubble.right(),
        "outgoing status stays outside the bubble, on its right, and aligns with its bottom edge");

    const QRect incomingReactions = DecorationHelper::reactionDecorationRect(outgoingBubble,
        QSize(30, 12), QSize(400, 300), false);
    const QRect outgoingReactions = DecorationHelper::reactionDecorationRect(outgoingBubble,
        QSize(30, 12), QSize(400, 300), true);
    passed &= check(!incomingReactions.intersects(outgoingBubble) &&
        incomingReactions.left() == outgoingBubble.left() &&
        incomingReactions.top() > outgoingBubble.bottom(),
        "incoming reactions start below the bubble's left corner");
    passed &= check(!outgoingReactions.intersects(outgoingBubble) &&
        outgoingReactions.right() == outgoingBubble.right() &&
        outgoingReactions.top() > outgoingBubble.bottom(),
        "outgoing reactions align below the bubble's lower-right corner");

    QTextDocument sourceWithDuplicates;
    QTextCursor tableCursor(&sourceWithDuplicates);
    QTextTable *sourceTable = tableCursor.insertTable(1, 1);
    QTextTableCell sourceCell = sourceTable->cellAt(0, 0);
    QTextCursor sourceContent = sourceCell.firstCursorPosition();
    sourceContent.insertText(QStringLiteral("duplicate source text"));
    QTextImageFormat sourceImage;
    sourceImage.setName(QStringLiteral("duplicate-source-image.png"));
    sourceContent.insertImage(sourceImage);
    QTextCursor sourceSpacer = DecorationHelper::replaceSourceMessageWithSpacer(sourceCell, 32.0);
    passed &= check(!sourceWithDuplicates.toPlainText().contains(QStringLiteral("duplicate source text")) &&
        !sourceWithDuplicates.toHtml().contains(QStringLiteral("duplicate-source-image.png")),
        "source message cleanup removes duplicate text and inline images");
    passed &= check(sourceSpacer.blockFormat().lineHeightType() == QTextBlockFormat::FixedHeight &&
        sourceSpacer.blockFormat().lineHeight() == 32.0,
        "source cleanup retains only a fixed-height empty flow spacer");
    DecorationHelper::setSourceSpacerHeight(sourceSpacer, 48.0);
    passed &= check(sourceSpacer.blockFormat().lineHeight() == 48.0,
        "external reactions can reserve extra source-document flow height");

    return passed ? 0 : 1;
}
