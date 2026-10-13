#ifndef MATRIXIMAGEDISPLAY_H
#define MATRIXIMAGEDISPLAY_H

#include <QSize>
#include <QUrl>
#include <QUrlQuery>
#include <QtGlobal>

namespace MatrixImageDisplay
{
inline QSize sizeForDisplay(const QSize &sourceSize, int availableWidth)
{
	Q_UNUSED(availableWidth);
	if (sourceSize.width() <= 0 || sourceSize.height() <= 0)
		return QSize();

	constexpr int displayWidth = 200;
	const int displayHeight = qMax(1, qRound(
		qreal(sourceSize.height()) * displayWidth / sourceSize.width()));
	return QSize(displayWidth, displayHeight);
}

inline QSize sizeForViewer(const QSize &sourceSize, const QSize &availableSize)
{
	if (sourceSize.width() <= 0 || sourceSize.height() <= 0 ||
		availableSize.width() <= 0 || availableSize.height() <= 0)
		return QSize();
	return sourceSize.scaled(availableSize, Qt::KeepAspectRatio);
}

inline QSize initialViewerSize(const QSize &sourceSize)
{
	if (sourceSize.width() <= 0 || sourceSize.height() <= 0)
		return QSize(320, 240);
	const QSize imageSize = sourceSize.scaled(QSize(480, 320), Qt::KeepAspectRatio);
	return QSize(qBound(220, imageSize.width() + 20, 500),
		qBound(180, imageSize.height() + 80, 400));
}

inline QUrl viewerUrl(const QString &conversationId, const QString &messageId)
{
	QUrl url;
	url.setScheme(QStringLiteral("vacuum-media"));
	url.setHost(QStringLiteral("open"));
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("room"), conversationId);
	query.addQueryItem(QStringLiteral("event"), messageId);
	url.setQuery(query);
	return url;
}

inline QUrl mediaLoadUrl(const QString &conversationId, const QString &messageId,
	const QString &mediaUrl, const QString &messageType, const QString &body, bool historical)
{
	QUrl url;
	url.setScheme(QStringLiteral("vacuum-media"));
	url.setHost(QStringLiteral("load"));
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("room"), conversationId);
	query.addQueryItem(QStringLiteral("event"), messageId);
	query.addQueryItem(QStringLiteral("url"), mediaUrl);
	query.addQueryItem(QStringLiteral("msgtype"), messageType);
	query.addQueryItem(QStringLiteral("body"), body);
	query.addQueryItem(QStringLiteral("historical"), historical
		? QStringLiteral("true") : QStringLiteral("false"));
	url.setQuery(query);
	return url;
}

inline QString previewHtml(const QUrl &openUrl, const QUrl &resourceUrl,
	const QString &altText, const QSize &sourceSize)
{
	const QSize displaySize = sizeForDisplay(sourceSize, 0);
	if (displaySize.isEmpty())
		return QString();
	return QStringLiteral("<div><a href=\"%1\"><img src=\"%2\" alt=\"%3\" width=\"%4\" height=\"%5\" /></a></div>")
		.arg(openUrl.toString(QUrl::FullyEncoded).toHtmlEscaped(),
			resourceUrl.toString(QUrl::FullyEncoded).toHtmlEscaped(), altText.toHtmlEscaped())
		.arg(displaySize.width())
		.arg(displaySize.height());
}
}

#endif // MATRIXIMAGEDISPLAY_H
