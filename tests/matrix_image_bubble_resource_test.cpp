#include <QApplication>
#include <QColor>
#include <QDateTime>
#include <QFrame>
#include <QImage>
#include <QNetworkAccessManager>
#include <QPainter>
#include <QRectF>
#include <QTextBrowser>
#include <QTextDocument>
#include <QUrl>
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

	const QString messageId = QStringLiteral("$hydrated-image-event");
	const QUrl resourceUrl(QStringLiteral("vacuum-matrix-image:/room/event"));
	IMessageContentOptions messageOptions;
	messageOptions.kind = IMessageContentOptions::KindMessage;
	messageOptions.messageId = messageId;
	messageOptions.senderId = QStringLiteral("@peer:example.invalid");
	messageOptions.senderName = QStringLiteral("Peer");
	messageOptions.time = QDateTime::currentDateTimeUtc();
	messageOptions.timeFormat = QStringLiteral("HH:mm");
	style.appendContent(view, QStringLiteral("<p>Load image</p>"), messageOptions);

	QImage sourceImage(16, 16, QImage::Format_ARGB32_Premultiplied);
	sourceImage.fill(QColor(20, 220, 40));
	view->document()->addResource(QTextDocument::ImageResource, resourceUrl, sourceImage);
	const QString imageHtml = QStringLiteral(
		"<div><img src=\"%1\" alt=\"test image\" width=\"200\" height=\"200\" /></div>")
		.arg(resourceUrl.toString(QUrl::FullyEncoded).toHtmlEscaped());
	const bool replaced = style.replaceMessageContent(view, messageId, imageHtml, messageId, false);
	application.processEvents();

	const QList<QFrame *> bubbles = view->findChildren<QFrame *>(QStringLiteral("modernChatBubbleFrame"));
	bool passed = check(replaced, "the placeholder bubble is replaced");
	passed &= check(bubbles.size() == 1, "hydration keeps exactly one bubble");
	QTextBrowser *content = bubbles.isEmpty() ? nullptr :
		bubbles.first()->findChild<QTextBrowser *>(QStringLiteral("modernChatBubbleContent"));
	passed &= check(content != nullptr, "the hydrated bubble has a content document");
	if (content) {
		const QImage bubbleImage = content->document()->resource(
			QTextDocument::ImageResource, resourceUrl).value<QImage>();
		passed &= check(!bubbleImage.isNull(), "the bubble document retains the decoded image resource");
		if (!bubbleImage.isNull())
			passed &= check(bubbleImage.pixelColor(bubbleImage.width() / 2,
				bubbleImage.height() / 2).green() > 150,
				"the bubble resource contains the decoded image pixels");

		QTextDocument *document = content->document();
		document->setTextWidth(300.0);
		QImage rendered(QSize(320, 240), QImage::Format_ARGB32_Premultiplied);
		rendered.fill(Qt::transparent);
		{
			QPainter painter(&rendered);
			document->drawContents(&painter, QRectF(0, 0, rendered.width(), rendered.height()));
		}
		bool visibleImagePixel = false;
		for (int y = 0; y < rendered.height() && !visibleImagePixel; ++y)
			for (int x = 0; x < rendered.width(); ++x) {
				const QColor pixel = rendered.pixelColor(x, y);
				if (pixel.green() > 150 && pixel.green() > pixel.red() * 2 && pixel.green() > pixel.blue()) {
					visibleImagePixel = true;
					break;
				}
			}
		passed &= check(visibleImagePixel, "the bubble document renders visible decoded-image pixels");
	}

	delete view;
	return passed ? 0 : 1;
}
