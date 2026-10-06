#include <QApplication>
#include <QAbstractTextDocumentLayout>
#include <QDateTime>
#include <QEventLoop>
#include <QFrame>
#include <QImage>
#include <QKeyEvent>
#include <QNetworkAccessManager>
#include <QPainter>
#include <QPointer>
#include <QRectF>
#include <QResizeEvent>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QTextBrowser>
#include <QTextTable>
#include <QTimeZone>
#include <QTimer>
#include <QWheelEvent>
#include <iostream>

#include "simplemessagestyle.h"
#include <utils/matrixhtml.h>

namespace {
bool check(bool condition, const char *description)
{
    if (!condition)
        std::cerr << description << " failed\n";
    return condition;
}

bool imageContainsColor(const QImage &image, const QColor &color)
{
    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            if (image.pixelColor(x, y) == color)
                return true;
        }
    }
    return false;
}

void waitForTimeout(int milliseconds)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

void sendWheel(StyleViewer *view, int angleDeltaY)
{
    const QPoint position = view->viewport()->rect().center();
    QWheelEvent event(QPointF(position), QPointF(view->viewport()->mapToGlobal(position)),
        QPoint(), QPoint(0, angleDeltaY), Qt::NoButton, Qt::NoModifier,
        Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(view->viewport(), &event);
}

bool checkLastBubbleFrame(StyleViewer *view, const QColor &fill, const QString &messageText,
    const char *description)
{
    const QList<QFrame *> frames = view->findChildren<QFrame *>(QStringLiteral("modernChatBubbleFrame"));
    if (frames.isEmpty())
        return check(false, description);

    QFrame *frame = frames.constLast();
    const QRect bubbleRect = frame->geometry();
    if (!view->viewport()->rect().contains(bubbleRect))
        return check(false, description);

    QImage image(view->viewport()->size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    view->viewport()->render(&painter);
    const int centerX = bubbleRect.center().x();
    const int centerY = bubbleRect.center().y();
    const QPoint corners[] = {bubbleRect.topLeft(), bubbleRect.topRight(),
        bubbleRect.bottomLeft(), bubbleRect.bottomRight()};
    bool rounded = bubbleRect.width() > 12 && bubbleRect.height() > 12;
    for (const QPoint &corner : corners)
        rounded = rounded && image.pixelColor(corner) != fill;
    rounded = rounded && image.pixelColor(centerX, bubbleRect.top() + 2) == fill &&
        image.pixelColor(bubbleRect.left() + 2, centerY) == fill;

    QTextBrowser *content = frame->findChild<QTextBrowser *>(QStringLiteral("modernChatBubbleContent"));
    const bool containsText = content && content->toPlainText().contains(messageText);
    const bool transparentContent = content && !content->viewport()->autoFillBackground() &&
        image.pixelColor(bubbleRect.right() - 7, centerY) == fill;
    bool passed = check(rounded, description);
    passed = check(containsText, "QFrame rich-text child preserves message text") && passed;
    passed = check(transparentContent, "QFrame rich-text child background is transparent") && passed;
    if (content)
    {
        const qreal heightSlack = frame->height() - content->document()->size().height();
        if (heightSlack < 8.0 || heightSlack > 18.0)
            std::cerr << "bubble vertical padding=" << heightSlack << "px; expected 8–18px\n";
        passed = check(heightSlack >= 8.0 && heightSlack <= 18.0,
            "bubble height contains only its intended vertical padding") && passed;
    }
    return passed;
}

QString tallMessage(int index)
{
    QString html = QStringLiteral("<p>Message %1").arg(index);
    for (int line = 0; line < 12; ++line)
        html += QStringLiteral("<br/>line %1 of message %2").arg(line).arg(index);
    html += QStringLiteral("</p>");
    return html;
}

}

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    QNetworkAccessManager networkAccessManager;
    SimpleMessageStyle style(QStringLiteral(AUTOSCROLL_STYLE_PATH), &networkAccessManager, nullptr);
    if (!style.isValid())
    {
        std::cerr << "message-style fixture is invalid\n";
        return 2;
    }

    IMessageStyleOptions styleOptions;
    styleOptions.extended.insert(MSO_STYLE_ID, style.styleId());
    styleOptions.extended.insert(MSO_VARIANT,
        style.infoValues().value(MSIV_DEFAULT_VARIANT).toString());
    StyleViewer *view = qobject_cast<StyleViewer *>(style.createWidget(styleOptions, nullptr));
    if (!view)
    {
        std::cerr << "message-style widget was not created\n";
        return 2;
    }

    view->resize(320, 160);
    view->show();
    application.processEvents();

    IMessageContentOptions options;
    options.senderId = QStringLiteral("peer");
    options.senderName = QStringLiteral("Peer");
    options.time = QDateTime::currentDateTime();
    options.timeFormat = QStringLiteral("HH:mm");
    for (int index = 0; index < 40; ++index)
        style.appendContent(view, tallMessage(index), options);
    application.processEvents();

    QScrollBar *scrollBar = view->verticalScrollBar();
    if (scrollBar->maximum() <= 0)
    {
        std::cerr << "test fixture did not create a scrollable conversation\n";
        delete view;
        return 2;
    }

    bool passed = check(scrollBar->value() == scrollBar->maximum(),
        "new messages keep a pinned conversation at the latest message");

    scrollBar->triggerAction(QAbstractSlider::SliderToMaximum);
    application.processEvents();
    const QSize oldSize = view->size();
    QResizeEvent resizeEvent(oldSize + QSize(1, 1), oldSize);
    QCoreApplication::sendEvent(view, &resizeEvent);
    scrollBar->triggerAction(QAbstractSlider::SliderPageStepSub);
    application.processEvents();

    if (scrollBar->value() >= scrollBar->maximum())
    {
        std::cerr << "scrollbar action did not move the view upward for the test\n";
        delete view;
        return 2;
    }

    waitForTimeout(180);
    passed &= check(scrollBar->value() < scrollBar->maximum(),
        "a pending resize scroll does not override the user's upward scroll");

    const int pausedValue = scrollBar->value();
    style.appendContent(view, tallMessage(40), options);
    application.processEvents();
    passed &= check(scrollBar->value() < scrollBar->maximum() && scrollBar->value() <= pausedValue,
        "new content does not resume scrolling while the user is reading older messages");

    scrollBar->triggerAction(QAbstractSlider::SliderToMaximum);
    application.processEvents();
    style.appendContent(view, tallMessage(41), options);
    application.processEvents();
    passed &= check(scrollBar->value() == scrollBar->maximum(),
        "scrolling manually to the bottom resumes following new messages");

    const QSize wheelResizeOldSize = view->size();
    QResizeEvent wheelResizeEvent(wheelResizeOldSize + QSize(1, 1), wheelResizeOldSize);
    QCoreApplication::sendEvent(view, &wheelResizeEvent);
    sendWheel(view, 120);
    application.processEvents();
    if (scrollBar->value() >= scrollBar->maximum())
    {
        std::cerr << "wheel event did not move the view upward for the test\n";
        delete view;
        return 2;
    }
    waitForTimeout(180);
    passed &= check(scrollBar->value() < scrollBar->maximum(),
        "mouse-wheel scrolling up also cancels pending automatic scrolling");

    scrollBar->triggerAction(QAbstractSlider::SliderToMaximum);
    application.processEvents();
    scrollBar->setValue(0);
    const QSize previewResizeOldSize = view->size();
    QResizeEvent previewResizeEvent(previewResizeOldSize + QSize(1, 1), previewResizeOldSize);
    QCoreApplication::sendEvent(view, &previewResizeEvent);
    options.noScroll = true;
    style.appendContent(view, tallMessage(42), options);
    application.processEvents();
    waitForTimeout(180);
    passed &= check(scrollBar->value() < scrollBar->maximum(),
        "explicit noScroll content preserves the preview scroll position");

    scrollBar->triggerAction(QAbstractSlider::SliderToMaximum);
    application.processEvents();
    const QSize keyboardResizeOldSize = view->size();
    QResizeEvent keyboardResizeEvent(keyboardResizeOldSize + QSize(1, 1), keyboardResizeOldSize);
    QCoreApplication::sendEvent(view, &keyboardResizeEvent);
    view->setFocus();
    QKeyEvent pageUpEvent(QEvent::KeyPress, Qt::Key_PageUp, Qt::NoModifier);
    QCoreApplication::sendEvent(view, &pageUpEvent);
    application.processEvents();
    if (scrollBar->value() >= scrollBar->maximum())
    {
        std::cerr << "PageUp key did not move the view upward for the test\n";
        delete view;
        return 2;
    }
    waitForTimeout(180);
    passed &= check(scrollBar->value() < scrollBar->maximum(),
        "keyboard scrolling up also cancels pending automatic scrolling");

    style.changeOptions(view, styleOptions, true);
    IMessageContentOptions groupedOptions;
    groupedOptions.kind = IMessageContentOptions::KindMessage;
    groupedOptions.senderId = QStringLiteral("peer");
    groupedOptions.senderName = QStringLiteral("Peer");
    groupedOptions.timeFormat = QStringLiteral("HH:mm");
    groupedOptions.time = QDateTime(QDate(2026, 10, 4), QTime(12, 34), QTimeZone::utc());
    style.appendContent(view, QStringLiteral("first"), groupedOptions);
    groupedOptions.time = groupedOptions.time.addSecs(60);
    style.appendContent(view, QStringLiteral("consecutive"), groupedOptions);
    groupedOptions.time = groupedOptions.time.addSecs(25 * 60);
    style.appendContent(view, QStringLiteral("later"), groupedOptions);
    groupedOptions.senderId = QStringLiteral("another-peer");
    groupedOptions.time = groupedOptions.time.addSecs(60);
    style.appendContent(view, QStringLiteral("new sender"), groupedOptions);
    const QString groupedHtml = view->document()->toHtml();
    passed &= check(groupedHtml.contains(QStringLiteral("12:34")),
        "the first message in a consecutive group displays its time");
    passed &= check(!groupedHtml.contains(QStringLiteral("12:35")),
        "a consecutive message from the same sender omits its time");
    passed &= check(groupedHtml.contains(QStringLiteral("13:00")) &&
        groupedHtml.contains(QStringLiteral("13:01")),
        "a later message or a new sender starts a timestamped group");

    QTemporaryDir avatarDirectory;
    if (!avatarDirectory.isValid())
    {
        std::cerr << "avatar image fixture directory could not be created\n";
        delete view;
        return 2;
    }
    QImage avatarImage(16, 16, QImage::Format_ARGB32_Premultiplied);
    avatarImage.fill(Qt::magenta);
    const QString avatarPath = avatarDirectory.filePath(QStringLiteral("sender.png"));
    if (!avatarImage.save(avatarPath))
    {
        std::cerr << "avatar image fixture could not be saved\n";
        delete view;
        return 2;
    }

    SimpleMessageStyle avatarStyle(QStringLiteral(AVATAR_STYLE_PATH), &networkAccessManager, nullptr);
    if (!avatarStyle.isValid())
    {
        std::cerr << "avatar message-style fixture is invalid\n";
        delete view;
        return 2;
    }
    IMessageStyleOptions avatarStyleOptions;
    avatarStyleOptions.extended.insert(MSO_STYLE_ID, avatarStyle.styleId());
    avatarStyleOptions.extended.insert(MSO_VARIANT,
        avatarStyle.infoValues().value(MSIV_DEFAULT_VARIANT).toString());
    StyleViewer *avatarView = qobject_cast<StyleViewer *>(
        avatarStyle.createWidget(avatarStyleOptions, nullptr));
    if (!avatarView)
    {
        std::cerr << "avatar message-style widget was not created\n";
        delete view;
        return 2;
    }
    avatarView->resize(480, 240);
    avatarView->show();
    application.processEvents();
    IMessageContentOptions avatarOptions;
    avatarOptions.kind = IMessageContentOptions::KindMessage;
    avatarOptions.senderId = QStringLiteral("same-user");
    avatarOptions.senderName = QStringLiteral("Same User");
    avatarOptions.senderAvatar = avatarPath;
    avatarOptions.time = QDateTime::currentDateTime();
    avatarOptions.timeFormat = QStringLiteral("HH:mm");
    avatarStyle.appendContent(avatarView, QStringLiteral("avatar message one"), avatarOptions);
    avatarOptions.time = avatarOptions.time.addSecs(3 * 60);
    avatarStyle.appendContent(avatarView, QStringLiteral("avatar message two"), avatarOptions);
    application.processEvents();

    const QString avatarHtml = avatarView->document()->toHtml();
    passed &= check(avatarHtml.count(QStringLiteral("vacuum-avatar:")) == 2,
        "both messages from the same user reference one shared avatar resource");
    const QRegularExpression avatarResourceExpression(QStringLiteral("src=\\\"(vacuum-avatar:[^\\\"]+)\\\""));
    const QRegularExpressionMatch avatarResourceMatch = avatarResourceExpression.match(avatarHtml);
    passed &= check(avatarResourceMatch.hasMatch(),
        "message avatars use a shared in-memory resource URL instead of a file path");
    passed &= check(!avatarHtml.contains(avatarPath),
        "message HTML does not reopen the sender avatar file for each message");
    if (avatarResourceMatch.hasMatch())
    {
        const QUrl avatarResourceUrl(avatarResourceMatch.captured(1));
        waitForTimeout(1500);
        const QImage loadedAvatar = avatarView->document()->resource(
            QTextDocument::ImageResource, avatarResourceUrl).value<QImage>();
        passed &= check(!loadedAvatar.isNull(),
            "the shared message avatar resource is populated asynchronously");
        const QImage reusedAvatar = avatarView->document()->resource(
            QTextDocument::ImageResource, avatarResourceUrl).value<QImage>();
        passed &= check(loadedAvatar.cacheKey() == reusedAvatar.cacheKey(),
            "repeated messages reuse the same decoded avatar image object");
        passed &= check(loadedAvatar.size() == QSize(32, 32),
            "Modern Chat sender avatar resource is exactly 32x32");
        passed &= check(loadedAvatar.pixelColor(0, 0).alpha() < 64 &&
            loadedAvatar.pixelColor(loadedAvatar.width() / 2, loadedAvatar.height() / 2).alpha() > 240,
            "Modern Chat avatar corners are rounded without clipping the center");
    }

    QImage inlineImage(24, 24, QImage::Format_ARGB32_Premultiplied);
    inlineImage.fill(QColor(220, 30, 40));
    const QUrl inlineImageUrl(QStringLiteral("vacuum-matrix-image:/rounded-fixture"));
    avatarView->document()->addResource(QTextDocument::ImageResource, inlineImageUrl, inlineImage);
    avatarOptions.senderId = QStringLiteral("image-user");
    avatarOptions.senderName = QStringLiteral("Image User");
    const QString inlineImageHtml = QStringLiteral(
        "geometry probe <img src=\"%1\" width=\"24\" height=\"24\" alt=\"fixture\" />")
        .arg(inlineImageUrl.toString(QUrl::FullyEncoded).toHtmlEscaped());
    avatarStyle.appendContent(avatarView, inlineImageHtml, avatarOptions);

    QTextCursor geometryProbeCursor = avatarView->document()->find(QStringLiteral("geometry probe"));
    QTextTable *geometryProbeTable = geometryProbeCursor.currentTable();
    const QList<QFrame *> geometryProbeFrames = avatarView->findChildren<QFrame *>(
        QStringLiteral("modernChatBubbleFrame"));
    passed &= check(geometryProbeTable != nullptr && !geometryProbeFrames.isEmpty(),
        "the geometry probe maps to a rendered message bubble");
    if (geometryProbeTable && !geometryProbeFrames.isEmpty())
    {
        application.processEvents();
        QFrame *geometryProbeFrame = geometryProbeFrames.constLast();
        const QRect geometryBeforeDocumentMutation = geometryProbeFrame->geometry();
        geometryProbeCursor.movePosition(QTextCursor::EndOfBlock);
        geometryProbeCursor.insertText(QString(400, QLatin1Char('W')));

        QRectF expectedGeometry = avatarView->document()->documentLayout()->frameBoundingRect(geometryProbeTable);
        expectedGeometry.translate(-avatarView->horizontalScrollBar()->value(),
            -avatarView->verticalScrollBar()->value());
        const QRect expectedGeometryAfterMutation = expectedGeometry.toAlignedRect();
        passed &= check(expectedGeometryAfterMutation != geometryBeforeDocumentMutation,
            "the document mutation changes the bubble's layout bounds");
        passed &= check(geometryProbeFrame->geometry() == geometryBeforeDocumentMutation,
            "bubble geometry is not updated synchronously during document layout changes");

        application.processEvents();
        expectedGeometry = avatarView->document()->documentLayout()->frameBoundingRect(geometryProbeTable);
        expectedGeometry.translate(-avatarView->horizontalScrollBar()->value(),
            -avatarView->verticalScrollBar()->value());
        passed &= check(geometryProbeFrame->geometry() == expectedGeometry.toAlignedRect(),
            "deferred bubble geometry matches the completed document layout");
    }
    const QImage renderedInlineImage = avatarView->document()->resource(
        QTextDocument::ImageResource, inlineImageUrl).value<QImage>();
    passed &= check(renderedInlineImage.pixelColor(0, 0).alpha() < 128 &&
        renderedInlineImage.pixelColor(renderedInlineImage.width() / 2,
            renderedInlineImage.height() / 2).alpha() > 240,
        "Modern Chat inline image corners are rounded without clipping the center");

    const QColor delayedImageColor(20, 180, 60);
    QImage delayedImage(24, 24, QImage::Format_ARGB32_Premultiplied);
    delayedImage.fill(delayedImageColor);
    const QString delayedImagePath = avatarDirectory.filePath(QStringLiteral("late-inline.png"));
    if (!delayedImage.save(delayedImagePath))
    {
        std::cerr << "delayed inline image fixture could not be saved\n";
        delete avatarView;
        delete view;
        return 2;
    }
    const QString delayedImageSource = avatarView->cacheImageResource(delayedImagePath);
    const QUrl delayedImageUrl(delayedImageSource);
    const QString delayedImageHtml = QStringLiteral(
        "late image <img src=\"%1\" width=\"24\" height=\"24\" alt=\"late image\" />")
        .arg(delayedImageSource.toHtmlEscaped());
    avatarOptions.senderId = QStringLiteral("late-image-user");
    avatarOptions.senderName = QStringLiteral("Late Image User");
    avatarStyle.appendContent(avatarView, delayedImageHtml, avatarOptions);
    application.processEvents();
    const QList<QFrame *> delayedImageFrames = avatarView->findChildren<QFrame *>(
        QStringLiteral("modernChatBubbleFrame"));
    QTextBrowser *delayedImageContent = delayedImageFrames.isEmpty() ? nullptr :
        delayedImageFrames.constLast()->findChild<QTextBrowser *>(
            QStringLiteral("modernChatBubbleContent"));
    waitForTimeout(1500);
    const QImage loadedDelayedImage = avatarView->document()->resource(
        QTextDocument::ImageResource, delayedImageUrl).value<QImage>();
    const QImage bubbleDelayedImage = delayedImageContent ? delayedImageContent->document()->resource(
        QTextDocument::ImageResource, delayedImageUrl).value<QImage>() : QImage();
    passed &= check(loadedDelayedImage.size() == delayedImage.size() &&
        loadedDelayedImage.pixelColor(12, 12) == delayedImageColor,
        "the shared image scheduler populates a late inline-image resource");
    passed &= check(bubbleDelayedImage.size() == delayedImage.size() &&
        bubbleDelayedImage.pixelColor(12, 12) == delayedImageColor,
        "late inline images are propagated into the message bubble document");
    if (delayedImageContent)
    {
        QTextDocument *bubbleDocument = delayedImageContent->document();
        const QSize documentSize = bubbleDocument->size().toSize().expandedTo(QSize(1, 1));
        QImage renderedBubbleDocument(documentSize, QImage::Format_ARGB32_Premultiplied);
        renderedBubbleDocument.fill(Qt::transparent);
        QPainter painter(&renderedBubbleDocument);
        bubbleDocument->drawContents(&painter);
        painter.end();
        passed &= check(imageContainsColor(renderedBubbleDocument, delayedImageColor),
            "late inline images are actually painted by the message bubble document");
    }

    const QColor networkImageColor(180, 40, 200);
    const QUrl networkImageUrl(QStringLiteral("vacuum-matrix-image:/loaded-after-bubble"));
    const QString networkImageHtml = QStringLiteral(
        "network image <img src=\"%1\" width=\"20\" height=\"20\" alt=\"network image\" />")
        .arg(networkImageUrl.toString(QUrl::FullyEncoded).toHtmlEscaped());
    avatarOptions.senderId = QStringLiteral("network-image-user");
    avatarOptions.senderName = QStringLiteral("Network Image User");
    avatarStyle.appendContent(avatarView, networkImageHtml, avatarOptions);
    application.processEvents();
    const QList<QFrame *> networkImageFrames = avatarView->findChildren<QFrame *>(
        QStringLiteral("modernChatBubbleFrame"));
    QTextBrowser *networkImageContent = networkImageFrames.isEmpty() ? nullptr :
        networkImageFrames.constLast()->findChild<QTextBrowser *>(
            QStringLiteral("modernChatBubbleContent"));
    QImage networkImage(20, 20, QImage::Format_ARGB32_Premultiplied);
    networkImage.fill(networkImageColor);
    avatarView->document()->addResource(QTextDocument::ImageResource, networkImageUrl, networkImage);
    avatarView->resourceLoaded(networkImageUrl);
    application.processEvents();
    const QImage bubbleNetworkImage = networkImageContent ? networkImageContent->document()->resource(
        QTextDocument::ImageResource, networkImageUrl).value<QImage>() : QImage();
    passed &= check(bubbleNetworkImage.size() == networkImage.size() &&
        bubbleNetworkImage.pixelColor(10, 10) == networkImageColor,
        "resourceLoaded images reach the message bubble document");

    IMessageContentOptions mediaHydrationOptions = avatarOptions;
    mediaHydrationOptions.messageId = QStringLiteral("$media-hydration-test");
    mediaHydrationOptions.senderId = QStringLiteral("matrix-media-sender");
    mediaHydrationOptions.senderName = QStringLiteral("Matrix Media Sender");
    const QString placeholderHtml = QStringLiteral(
        "<a href=\"vacuum-media://load?room=test&amp;event=%24media-hydration-test\">Load image</a>");
    avatarStyle.appendContent(avatarView, placeholderHtml, mediaHydrationOptions);
    application.processEvents();
    const QList<QFrame *> placeholderFrames = avatarView->findChildren<QFrame *>(
        QStringLiteral("modernChatBubbleFrame"));
    QTextBrowser *placeholderContent = placeholderFrames.isEmpty() ? nullptr :
        placeholderFrames.constLast()->findChild<QTextBrowser *>(
            QStringLiteral("modernChatBubbleContent"));
    passed &= check(placeholderContent && placeholderContent->toPlainText().contains(
        QStringLiteral("Load image")),
        "Matrix image placeholder is present before history hydration");

    avatarStyle.changeOptions(avatarView, avatarStyleOptions, true);
    application.processEvents();
    passed &= check(avatarView->findChildren<QFrame *>(QStringLiteral("modernChatBubbleFrame")).isEmpty(),
        "history rebuild clears the rendered Matrix image placeholder");

    const QColor hydratedImageColor(40, 70, 220);
    const QUrl hydratedImageUrl(QStringLiteral("vacuum-matrix-image:/hydrated-event"));
    QImage hydratedImage(20, 20, QImage::Format_ARGB32_Premultiplied);
    hydratedImage.fill(hydratedImageColor);
    avatarView->document()->addResource(QTextDocument::ImageResource, hydratedImageUrl, hydratedImage);
    avatarView->resourceUpdated(hydratedImageUrl);
    const QString hydratedImageHtml = QStringLiteral(
        "<img src=\"%1\" width=\"20\" height=\"20\" alt=\"hydrated photo\" />")
        .arg(hydratedImageUrl.toString(QUrl::FullyEncoded).toHtmlEscaped());
    avatarStyle.appendContent(avatarView, hydratedImageHtml, mediaHydrationOptions);
    application.processEvents();
    const QList<QFrame *> hydratedImageFrames = avatarView->findChildren<QFrame *>(
        QStringLiteral("modernChatBubbleFrame"));
    QTextBrowser *hydratedImageContent = hydratedImageFrames.isEmpty() ? nullptr :
        hydratedImageFrames.constLast()->findChild<QTextBrowser *>(
            QStringLiteral("modernChatBubbleContent"));
    const QImage bubbleHydratedImage = hydratedImageContent ?
        hydratedImageContent->document()->resource(QTextDocument::ImageResource,
            hydratedImageUrl).value<QImage>() : QImage();
    passed &= check(hydratedImageFrames.size() == 1 && hydratedImageContent &&
        !hydratedImageContent->toPlainText().contains(QStringLiteral("Load image")) &&
        bubbleHydratedImage.size() == hydratedImage.size() &&
        bubbleHydratedImage.pixelColor(10, 10) == hydratedImageColor,
        "hydrated Matrix image replaces the placeholder in the rebuilt message bubble");
    if (hydratedImageContent)
    {
        QTextDocument *bubbleDocument = hydratedImageContent->document();
        const QSize documentSize = bubbleDocument->size().toSize().expandedTo(QSize(1, 1));
        QImage renderedBubbleDocument(documentSize, QImage::Format_ARGB32_Premultiplied);
        renderedBubbleDocument.fill(Qt::transparent);
        QPainter painter(&renderedBubbleDocument);
        bubbleDocument->drawContents(&painter);
        painter.end();
        passed &= check(imageContainsColor(renderedBubbleDocument, hydratedImageColor),
            "hydrated Matrix image is actually painted by the rebuilt bubble document");
    }

    const int bubbleFrameCountBefore = avatarView->findChildren<QFrame *>(
        QStringLiteral("modernChatBubbleFrame")).size();
    avatarStyle.appendContent(avatarView, QStringLiteral("bubble edge probe"), avatarOptions);
    application.processEvents();
    passed &= check(avatarView->findChildren<QFrame *>(QStringLiteral("modernChatBubbleFrame")).size() ==
        bubbleFrameCountBefore + 1, "Modern Chat creates one Qt-styled frame per message bubble");
    passed &= checkLastBubbleFrame(avatarView, QColor(QStringLiteral("#f1f2f4")),
        QStringLiteral("bubble edge probe"),
        "the Qt-styled message frame has rounded pixels and transparent rich-text content");
    const QUrl bubbleCornerUrl(QStringLiteral("vacuum-bubble:/incoming/top-left"));
    passed &= check(!avatarView->document()->resource(QTextDocument::ImageResource, bubbleCornerUrl).isValid(),
        "Modern Chat does not register bitmap resources for bubble corners");

    IMessageContentOptions incomingContentOptions = avatarOptions;
    incomingContentOptions.senderId = QStringLiteral("incoming-content-user");
    incomingContentOptions.senderName = QStringLiteral("Incoming Content User");
    incomingContentOptions.time = QDateTime::currentDateTime();
    avatarStyle.appendContent(avatarView, QStringLiteral("incoming content corner probe"), incomingContentOptions);
    application.processEvents();
    passed &= checkLastBubbleFrame(avatarView, QColor(QStringLiteral("#f1f2f4")),
        QStringLiteral("incoming content corner probe"),
        "incoming first-message QFrame renders rounded corners with rich text");

    IMessageContentOptions outgoingOptions = avatarOptions;
    outgoingOptions.direction = IMessageContentOptions::DirectionOut;
    outgoingOptions.senderId = QStringLiteral("outgoing-user");
    outgoingOptions.senderName = QStringLiteral("Outgoing User");
    outgoingOptions.time = QDateTime::currentDateTime();
    avatarStyle.appendContent(avatarView, QStringLiteral("outgoing bubble first"), outgoingOptions);
    application.processEvents();
    passed &= checkLastBubbleFrame(avatarView, QColor(QStringLiteral("#e8f1ff")),
        QStringLiteral("outgoing bubble first"),
        "outgoing first-message QFrame renders rounded corners with rich text");
    outgoingOptions.time = outgoingOptions.time.addSecs(60);
    avatarStyle.appendContent(avatarView, QStringLiteral("outgoing bubble probe"), outgoingOptions);
    application.processEvents();
    passed &= checkLastBubbleFrame(avatarView, QColor(QStringLiteral("#e8f1ff")),
        QStringLiteral("outgoing bubble probe"),
        "outgoing consecutive-message QFrame renders rounded corners with rich text");

    const QString singleLineMatrixHtml = matrixSafeHtml(
        matrixMarkdownToSafeHtml(QStringLiteral("hallo")));
    outgoingOptions.senderId = QStringLiteral("matrix-user");
    outgoingOptions.time = outgoingOptions.time.addSecs(60);
    avatarStyle.appendContent(avatarView, singleLineMatrixHtml, outgoingOptions);
    application.processEvents();
    const QList<QFrame *> matrixFrames = avatarView->findChildren<QFrame *>(
        QStringLiteral("modernChatBubbleFrame"));
    QTextBrowser *matrixContent = matrixFrames.isEmpty() ? nullptr :
        matrixFrames.constLast()->findChild<QTextBrowser *>(
            QStringLiteral("modernChatBubbleContent"));
    passed &= check(matrixContent && matrixContent->toPlainText().trimmed() == QStringLiteral("hallo"),
        "a one-line Matrix formatted body stays one line in the outgoing bubble");
    passed &= check(matrixContent && matrixContent->document()->blockCount() == 1,
        "a one-line Matrix formatted body does not create empty QTextDocument blocks");
    passed &= check(matrixContent && matrixContent->document()->size().height() <= 60.0,
        "a one-line Matrix formatted body does not create an over-height bubble");
    delete avatarView;

    StyleViewer lifetimeView(nullptr);
    lifetimeView.resize(320, 200);
    lifetimeView.show();
    QTextCursor lifetimeCursor(lifetimeView.document());
    QTextTable *lifetimeTable = lifetimeCursor.insertTable(1, 1);
    lifetimeTable->cellAt(0, 0).firstCursorPosition().insertText(QStringLiteral("lifetime probe"));
    lifetimeView.addMessageBubble(lifetimeTable, QStringLiteral("lifetime probe"), Qt::lightGray);
    application.processEvents();
    QPointer<QTextTable> deletedTableGuard(lifetimeTable);
    lifetimeView.document()->setHtml(QStringLiteral("<p>replacement document</p>"));
    lifetimeView.resize(321, 201);
    application.processEvents();
    passed &= check(deletedTableGuard.isNull(),
        "replacing the document destroys its previous message table");
    passed &= check(lifetimeView.findChildren<QFrame *>(QStringLiteral("modernChatBubbleFrame")).isEmpty(),
        "geometry updates discard bubbles whose document table was deleted");

    delete view;
    return passed ? 0 : 1;
}
