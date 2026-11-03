#include <QApplication>
#include <QColor>
#include <QDateTime>
#include <QFrame>
#include <QImage>
#include <QNetworkAccessManager>
#include <QPainter>
#include <QPointer>
#include <QRectF>
#include <QTextBrowser>
#include <QTextDocument>
#include <QUrl>
#include <iostream>

#include "simplemessagestyle.h"
#include "styleviewer.h"
#include "decorationhelper.h"
#include "../../src/plugins/chatmessagehandler/matriximagedisplay.h"

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

	const QString imageMessageId = QStringLiteral("$image-event:example.org");
	appendMessage(imageMessageId, QStringLiteral("<p>Load image</p>"));
	application.processEvents();
	QImage sourceImage(16, 12, QImage::Format_ARGB32_Premultiplied);
	sourceImage.fill(QColor(20, 220, 40));
	const QUrl resourceUrl(QStringLiteral("vacuum-matrix-image:/%1/%2")
		.arg(QString::fromLatin1(QUrl::toPercentEncoding(QStringLiteral("!room:example.org"))),
			QString::fromLatin1(QUrl::toPercentEncoding(imageMessageId))));
	view->document()->addResource(QTextDocument::ImageResource, resourceUrl, sourceImage);
	const QString imageHtml = MatrixImageDisplay::previewHtml(
		MatrixImageDisplay::viewerUrl(QStringLiteral("!room:example.org"), imageMessageId),
		resourceUrl, QStringLiteral("synthetic.png"), sourceImage.size());
	const QSet<QUrl> parsedResources = DecorationHelper::imageResourcesFromHtml(imageHtml);
	passed &= check(view->document()->resource(QTextDocument::ImageResource, resourceUrl).isValid(),
		"the parent document contains the encoded Matrix image resource before replacement");
	passed &= check(parsedResources.contains(resourceUrl),
		"the actual Matrix preview HTML parses back to the resource URL used by the handler");
	const bool imageReplaced = style.replaceMessageContent(view, imageMessageId, imageHtml,
		imageMessageId, false);
	application.processEvents();
	const QList<QFrame *> imageBubbles = view->findChildren<QFrame *>(QStringLiteral("modernChatBubbleFrame"));
	passed &= check(imageReplaced && imageBubbles.size() == 3,
		"the Matrix-URI image hydration replaces the existing bubble");
	QTextBrowser *imageContent = imageBubbles.size() < 3 ? nullptr :
		imageBubbles.at(2)->findChild<QTextBrowser *>(QStringLiteral("modernChatBubbleContent"));
	passed &= check(imageContent != nullptr, "the Matrix image bubble has a content document");
	if (imageContent) {
		const QImage bubbleImage = imageContent->document()->resource(
			QTextDocument::ImageResource, resourceUrl).value<QImage>();
		passed &= check(!bubbleImage.isNull() && bubbleImage.size() == sourceImage.size(),
			"the Matrix-URI resource survives replacement in the bubble document");
		QImage rendered(QSize(320, 240), QImage::Format_ARGB32_Premultiplied);
		rendered.fill(Qt::transparent);
		{
			QPainter painter(&rendered);
			imageContent->document()->drawContents(&painter, QRectF(0, 0, rendered.width(), rendered.height()));
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
		passed &= check(visibleImagePixel,
			"the Matrix-URI image renders visible pixels in the bubble document");
	}

	delete view;
	return passed ? 0 : 1;
}
