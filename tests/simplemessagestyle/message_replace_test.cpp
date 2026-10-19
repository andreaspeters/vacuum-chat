#include <QApplication>
#include <QDateTime>
#include <QFrame>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTextBrowser>
#include <iostream>

#include "simplemessagestyle.h"
#include "styleviewer.h"

namespace
{
bool check(bool condition, const char *description)
{
	if (!condition)
		std::cerr << description << " failed\n";
	return condition;
}
}

int main(int argc, char **argv)
{
	QApplication application(argc, argv);
	QNetworkAccessManager networkAccessManager;
	SimpleMessageStyle style(QStringLiteral(AVATAR_STYLE_PATH), &networkAccessManager, nullptr);
	if (!style.isValid()) {
		std::cerr << "Modern Chat style fixture is invalid\n";
		return 2;
	}

	IMessageStyleOptions styleOptions;
	styleOptions.extended.insert(MSO_STYLE_ID, style.styleId());
	styleOptions.extended.insert(MSO_VARIANT,
		style.infoValues().value(MSIV_DEFAULT_VARIANT).toString());
	StyleViewer *view = qobject_cast<StyleViewer *>(style.createWidget(styleOptions, nullptr));
	if (!view) {
		std::cerr << "Modern Chat style widget was not created\n";
		return 2;
	}
	view->resize(480, 240);
	view->show();
	application.processEvents();

	auto appendMessage = [&style, view](const QString &messageId, const QString &html) {
		IMessageContentOptions options;
		options.kind = IMessageContentOptions::KindMessage;
		options.messageId = messageId;
		options.senderId = QStringLiteral("peer");
		options.senderName = QStringLiteral("Peer");
		options.time = QDateTime::currentDateTimeUtc();
		options.timeFormat = QStringLiteral("HH:mm");
		style.appendContent(view, html, options);
	};
	appendMessage(QStringLiteral("$hydrating-event"), QStringLiteral("<p>Load image</p>"));
	appendMessage(QStringLiteral("$later-event"), QStringLiteral("<p>later message stays visible</p>"));
	application.processEvents();

	QList<QFrame *> before = view->findChildren<QFrame *>(QStringLiteral("modernChatBubbleFrame"));
	bool passed = check(before.size() == 2, "two message bubbles are initially visible");
	QPointer<QFrame> hydratedFrame = before.value(0);
	QPointer<QFrame> laterFrame = before.value(1);
	QTextBrowser *hydratedContent = hydratedFrame
		? hydratedFrame->findChild<QTextBrowser *>(QStringLiteral("modernChatBubbleContent")) : nullptr;
	QTextBrowser *laterContent = laterFrame
		? laterFrame->findChild<QTextBrowser *>(QStringLiteral("modernChatBubbleContent")) : nullptr;
	passed &= check(hydratedContent && laterContent, "both message bubbles expose their rendered content");

	const bool replaced = style.replaceMessageContent(view, QStringLiteral("$hydrating-event"),
		QStringLiteral("<p>hydrated image content</p>"));
	application.processEvents();
	const QList<QFrame *> after = view->findChildren<QFrame *>(QStringLiteral("modernChatBubbleFrame"));
	passed &= check(replaced, "the style replaces the matching message content");
	passed &= check(after.size() == 2 && hydratedFrame && laterFrame &&
		after.at(0) == hydratedFrame && after.at(1) == laterFrame,
		"replacement preserves both existing bubble frames and does not append another bubble");
	passed &= check(hydratedContent && hydratedContent->toPlainText().contains(
		QStringLiteral("hydrated image content")) &&
		!hydratedContent->toPlainText().contains(QStringLiteral("Load image")),
		"the matching bubble shows the hydrated content instead of its placeholder");
	passed &= check(laterContent && laterContent->toPlainText().contains(
		QStringLiteral("later message stays visible")),
		"an unrelated later bubble remains unchanged");

	delete view;
	return passed ? 0 : 1;
}
