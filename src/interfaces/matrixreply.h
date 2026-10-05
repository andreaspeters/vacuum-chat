#ifndef MATRIXREPLY_H
#define MATRIXREPLY_H

#include <QJsonObject>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

namespace MatrixReply {
inline bool isEventId(const QString &eventId)
{
	return eventId.size() > 1 && eventId.startsWith(QLatin1Char('$'));
}

inline QJsonObject relation(const QString &eventId)
{
	if (!isEventId(eventId))
		return QJsonObject();
	return QJsonObject{{QStringLiteral("m.in_reply_to"),
		QJsonObject{{QStringLiteral("event_id"), eventId}}}};
}

inline QJsonObject applyRelation(QJsonObject content, const QString &eventId)
{
	const QJsonObject replyRelation = relation(eventId);
	if (replyRelation.isEmpty())
		return content;
	content.insert(QStringLiteral("m.relates_to"), replyRelation);
	content.insert(QStringLiteral("m.mentions"), QJsonObject());
	return content;
}

inline QString replyEventId(const QJsonObject &content)
{
	const QJsonObject relation = content.value(QStringLiteral("m.relates_to")).toObject();
	const QString nestedEventId = relation.value(QStringLiteral("m.in_reply_to")).toObject()
		.value(QStringLiteral("event_id")).toString();
	if (!nestedEventId.isEmpty())
		return nestedEventId;
	return content.value(QStringLiteral("m.in_reply_to")).toObject()
		.value(QStringLiteral("event_id")).toString();
}

inline QString stripFallbackText(const QString &body)
{
	const QStringList lines = body.split(QLatin1Char('\n'));
	if (lines.isEmpty() ||
		!QRegularExpression(QStringLiteral("^> <@[^>\\r\\n]+>"))
			.match(lines.first()).hasMatch())
		return body;

	for (int index = 1; index < lines.size(); ++index) {
		if (lines.at(index).trimmed().isEmpty())
			return lines.mid(index + 1).join(QLatin1Char('\n'));
		if (!lines.at(index).startsWith(QLatin1Char('>')))
			return body;
	}
	return body;
}

inline QString stripFallbackHtml(const QString &html)
{
	const QRegularExpression fallback(QStringLiteral(
		"^\\s*<mx-reply\\b[^>]*>.*?</mx-reply\\s*>\\s*"),
		QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
	const QRegularExpressionMatch match = fallback.match(html);
	return match.hasMatch() ? html.mid(match.capturedLength()) : html;
}

inline QString previewHtml(const QString &senderName, const QString &excerpt,
	const QString &avatarResourceUrl)
{
	QString avatarHtml;
	if (!avatarResourceUrl.isEmpty())
		avatarHtml = QStringLiteral("<img src=\"%1\" width=\"18\" height=\"18\" "
			"style=\"vertical-align:middle;\" /> ").arg(avatarResourceUrl.toHtmlEscaped());
	const QString escapedExcerpt = excerpt.toHtmlEscaped().replace(
		QStringLiteral("\n"), QStringLiteral("<br/>"));
	return QStringLiteral(
		"<div style=\"border-left:2px solid #8b5cf6; margin:2px 0 6px 0; padding:2px 0 2px 8px;\">"
		"<div style=\"color:#c5b4e5; font-weight:bold; font-size:small;\">%1%2</div>"
		"<div style=\"color:#9a9a9a; font-size:small;\">%3</div></div>")
		.arg(avatarHtml, senderName.toHtmlEscaped(), escapedExcerpt);
}
}

#endif // MATRIXREPLY_H
