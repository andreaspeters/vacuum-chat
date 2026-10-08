#include "decorationhelper.h"
#include "../../interfaces/messagedecorationpolicy.h"

#include <QCoreApplication>
#include <QTextCursor>
#include <QTextDocument>
#include <cstdio>

namespace
{
bool check(bool condition, const char *description)
{
    if (!condition)
        std::fprintf(stderr, "FAIL: %s\n", description);
    return condition;
}
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QMap<QString, DecorationHelper::Decoration> decorations;
    DecorationHelper::createOrUpdateDecoration(decorations, QStringLiteral("status"),
        QStringLiteral("<div>sent</div>"), 0, 0);
    bool passed = check(DecorationHelper::renderBubbleHtml(QStringLiteral("<p>message</p>"), decorations) ==
        QStringLiteral("<p>message</p><div>sent</div>"),
        "visible bubble HTML includes a newly added decoration");

    QTextDocument sourceDocument;
    sourceDocument.setPlainText(QStringLiteral("source message"));
    QTextCursor sourceCursor(&sourceDocument);
    sourceCursor.select(QTextCursor::Document);
    DecorationHelper::hideSourceMessageText(sourceCursor);
    QTextCursor sourceProbe(&sourceDocument);
    sourceProbe.setPosition(0);
    passed &= check(sourceDocument.toPlainText() == QStringLiteral("source message"),
        "hiding source message text preserves its document content and layout text");
    passed &= check(sourceProbe.charFormat().foreground().color().alpha() == 0,
        "source message glyphs are made transparent so only the bubble copy is visible");

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

    passed &= check(DecorationHelper::overlayHeightForContent(18.0, 4.0) == 22.0,
        "bubble height follows visible content plus only explicit compact padding");

    DecorationHelper::BubbleGeometrySnapshot bubbleGeometry;
    const QRectF initialTableRect(12.0, 24.0, 180.0, 140.0);
    const bool initialized = DecorationHelper::initializeBubbleGeometry(
        bubbleGeometry, initialTableRect, 18.0, 10.0);
    const QRectF initialBubbleRect = bubbleGeometry.documentRect;
    const bool initializedAgain = DecorationHelper::initializeBubbleGeometry(
        bubbleGeometry, QRectF(40.0, 80.0, 260.0, 500.0), 90.0, 10.0);
    passed &= check(initialized && bubbleGeometry.initialized &&
        initialBubbleRect == QRectF(12.0, 24.0, 180.0, 28.0),
        "new bubble geometry is captured from its first stable content measurement");
    passed &= check(!initializedAgain && bubbleGeometry.documentRect == initialBubbleRect,
        "later document/layout updates cannot resize or rebuild an existing bubble");

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
        QStringLiteral("matrix-reactions"), reactionsHtml, 0, 0);
    passed &= check(DecorationHelper::renderBubbleHtml(QStringLiteral("<p>message</p>"),
        positionedDecorations) == QStringLiteral("<p>message</p>") + reactionsHtml,
        "successful-send status is excluded from bubble content while reactions remain there");
    passed &= check(DecorationHelper::renderAdjacentHtml(positionedDecorations) == statusHtml,
        "successful-send status is rendered as an adjacent decoration");

    const QRect outgoingBubble(150, 100, 80, 40);
    const QRect outgoingStatus = DecorationHelper::adjacentDecorationRect(outgoingBubble,
        QSize(20, 12), QSize(400, 300), true);
    passed &= check(!outgoingStatus.intersects(outgoingBubble) &&
        outgoingStatus.bottom() == outgoingBubble.bottom() &&
        outgoingStatus.left() > outgoingBubble.right(),
        "outgoing status stays outside the bubble, on its right, and aligns with its bottom edge");

    return passed ? 0 : 1;
}
