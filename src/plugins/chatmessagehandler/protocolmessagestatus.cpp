#include "protocolmessagestatus.h"

#include <QByteArray>
#include <QChar>
#include <QStringList>
#include <QUrl>

namespace
{
QString escapedTooltip(const QString &text)
{
	QString escaped = text.toHtmlEscaped();
	escaped.replace(QLatin1Char('\r'), QString());
	escaped.replace(QLatin1Char('\n'), QStringLiteral("&#10;"));
	return escaped;
}

QString readerInitial(const QString &name)
{
	if (name.isEmpty())
		return QStringLiteral("?");

	if (name.size() > 1 && name.at(0).isHighSurrogate() && name.at(1).isLowSurrogate())
		return name.left(2);
	return name.left(1);
}

QString readerColor(const QString &name)
{
	static const char *colors[] = {
		"#4A6FA5", "#6D597A", "#3D8361", "#A65E39",
		"#8A4F7D", "#587291", "#9B6B3D", "#5C677D"
	};
	static const uint colorCount = sizeof(colors) / sizeof(colors[0]);

	uint hash = 2166136261u;
	const QByteArray bytes = name.toUtf8();
	for (char byte : bytes) {
		hash ^= static_cast<unsigned char>(byte);
		hash *= 16777619u;
	}
	return QString::fromLatin1(colors[hash % colorCount]);
}

bool isLocalAvatarResource(const QString &value)
{
	const QUrl url(value);
	return url.isValid() && url.scheme() == QStringLiteral("vacuum-avatar") &&
		url.host().isEmpty() && url.userInfo().isEmpty() && url.fragment().isEmpty();
}

QString messageStateKey(const QString &streamId, const QString &conversationId,
	const QString &messageId)
{
	return streamId + QChar(0x1f) + conversationId + QChar(0x1f) + messageId;
}
}

namespace ProtocolMessageStatus
{
QString buildReadReceiptFooter(bool isSentSuccess, const QList<QString> &readerNames,
	const QList<QString> &avatarUrls)
{
	if (!isSentSuccess && readerNames.isEmpty())
		return QString();

	QString html = QStringLiteral(
		"<div style=\"text-align:right;white-space:nowrap;line-height:10px;margin-top:2px;\">");
	if (isSentSuccess) {
		html += QStringLiteral(
			"<span style=\"display:inline-block;color:#888;font-size:10px;line-height:10px;vertical-align:middle;\" "
			"title=\"Gesendet\">&#10003;</span>");
		if (!readerNames.isEmpty())
			html += QStringLiteral("&nbsp;");
	}

	QStringList names;
	for (const QString &readerName : readerNames) {
		const QString name = readerName.trimmed();
		names.append(name.isEmpty() ? QStringLiteral("?") : name);
	}

	for (int i = 0; i < names.size(); ++i) {
		const QString &name = names.at(i);
		const QString tooltip = escapedTooltip(i == 0 && names.size() > 1
			? names.join(QLatin1Char('\n')) : name);
		if (i > 0)
			html += QStringLiteral("&nbsp;");

		const QString avatarUrl = avatarUrls.value(i);
		if (isLocalAvatarResource(avatarUrl)) {
			html += QStringLiteral(
				"<img src=\"%1\" width=\"10\" height=\"10\" alt=\"\" "
				"style=\"width:10px;height:10px;border-radius:50%;vertical-align:middle;object-fit:cover;\" "
				"title=\"%2\"/>").arg(avatarUrl.toHtmlEscaped(), tooltip);
		} else {
			html += QStringLiteral(
				"<span style=\"display:inline-block;width:10px;height:10px;line-height:10px;"
				"border-radius:50%;text-align:center;vertical-align:middle;"
				"font-size:7px;font-weight:600;color:#fff;background-color:%1;\" title=\"%2\">%3</span>")
				.arg(readerColor(name), tooltip, readerInitial(name).toHtmlEscaped());
		}
	}

	html += QStringLiteral("</div>");
	return html;
}

void StateStore::updateDelivery(const QString &streamId, const QString &conversationId,
	const QString &transactionId, const QString &status, const QString &serverEventId)
{
	const bool sentSuccessfully = status == QStringLiteral("sent");
	const QString transactionKey = messageStateKey(streamId, conversationId, transactionId);
	const QString eventKey = messageStateKey(streamId, conversationId, serverEventId);
	if (!transactionId.isEmpty()) {
		FStates[transactionKey].sentSuccessfully = sentSuccessfully;
		if (!serverEventId.isEmpty())
			FAliases.insert(transactionKey, serverEventId);
	}
	if (!serverEventId.isEmpty()) {
		FStates[eventKey].sentSuccessfully = sentSuccessfully;
		if (!transactionId.isEmpty())
			FAliases.insert(eventKey, transactionId);
	}
}

void StateStore::updateReadReceipt(const QString &streamId, const QString &conversationId,
	const QString &eventId, const QString &readerId, const QString &displayName)
{
	if (streamId.isEmpty() || conversationId.isEmpty() || eventId.isEmpty() || readerId.isEmpty())
		return;

	QList<Reader> &readers = FStates[messageStateKey(streamId, conversationId, eventId)].readers;
	for (Reader &reader : readers) {
		if (reader.userId == readerId) {
			if (!displayName.isEmpty())
				reader.displayName = displayName;
			return;
		}
	}
	readers.append({readerId, displayName.isEmpty() ? readerId : displayName});
}

MessageState StateStore::stateFor(const QString &streamId, const QString &conversationId,
	const QString &messageId, const QString &transactionId) const
{
	MessageState result;
	QStringList ids;
	if (!messageId.isEmpty())
		ids.append(messageId);
	if (!transactionId.isEmpty() && !ids.contains(transactionId))
		ids.append(transactionId);
	for (int i = 0; i < ids.size(); ++i) {
		const QString alias = FAliases.value(messageStateKey(streamId, conversationId, ids.at(i)));
		if (!alias.isEmpty() && !ids.contains(alias))
			ids.append(alias);
	}
	for (const QString &id : ids) {
		if (id.isEmpty())
			continue;
		const MessageState state = FStates.value(messageStateKey(streamId, conversationId, id));
		result.sentSuccessfully = result.sentSuccessfully || state.sentSuccessfully;
		for (const Reader &reader : state.readers) {
			bool found = false;
			for (Reader &existing : result.readers) {
				if (existing.userId == reader.userId) {
					if (!reader.displayName.isEmpty())
						existing.displayName = reader.displayName;
					found = true;
					break;
				}
			}
			if (!found)
				result.readers.append(reader);
		}
	}
	return result;
}
}
