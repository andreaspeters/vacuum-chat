#include <QApplication>
#include <QDateTime>
#include <QEventLoop>
#include <QImage>
#include <QKeyEvent>
#include <QNetworkAccessManager>
#include <QResizeEvent>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QTimer>
#include <QWheelEvent>
#include <iostream>

#include "simplemessagestyle.h"

namespace {
bool check(bool condition, const char *description)
{
    if (!condition)
        std::cerr << description << " failed\n";
    return condition;
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
    }
    delete avatarView;

    delete view;
    return passed ? 0 : 1;
}
