#include "matrixtextmessage.h"

#include <utils/matrixhtml.h>

namespace
{
QString withoutTrailingLineEndings(QString html)
{
	while (!html.isEmpty() && (html.endsWith(QLatin1Char('\r')) ||
		html.endsWith(QLatin1Char('\n'))))
		html.chop(1);
	return html;
}
}

QString matrixMessageTypeForMetadata(const QVariantMap &metadata)
{
	if (metadata.value(QStringLiteral("media_type")).toString() == QStringLiteral("image"))
		return QStringLiteral("m.image");
	const QString messageType = metadata.value(QStringLiteral("msgtype")).toString();
	return messageType.isEmpty() ? QStringLiteral("m.text") : messageType;
}

MatrixTextMessagePayload createMatrixTextMessagePayload(
	const QString &body, const QVariantMap &metadata)
{
	const QString suppliedFormattedBody = metadata.value(QStringLiteral("formatted_body")).toString();
	const QString formattedBody = withoutTrailingLineEndings(matrixSafeHtml(suppliedFormattedBody.isEmpty()
		? matrixMarkdownToSafeHtml(body) : suppliedFormattedBody));

	MatrixTextMessagePayload payload;
	payload.content.insert(QStringLiteral("msgtype"), QStringLiteral("m.text"));
	payload.content.insert(QStringLiteral("body"), body);
	payload.content.insert(QStringLiteral("format"), QStringLiteral("org.matrix.custom.html"));
	payload.content.insert(QStringLiteral("formatted_body"), formattedBody);
	payload.content.insert(QStringLiteral("m.mentions"), QJsonObject());

	payload.localEchoMetadata = metadata;
	payload.localEchoMetadata.insert(QStringLiteral("msgtype"), QStringLiteral("m.text"));
	payload.localEchoMetadata.insert(QStringLiteral("format"), QStringLiteral("org.matrix.custom.html"));
	payload.localEchoMetadata.insert(QStringLiteral("formatted_body"), formattedBody);
	payload.localEchoMetadata.insert(QStringLiteral("m.mentions"), QVariantMap());
	return payload;
}

QJsonObject createMatrixEditMessageContent(const QString &body,
	const QVariantMap &metadata, const QString &originalEventId)
{
	if (body.isEmpty() || originalEventId.isEmpty())
		return QJsonObject();
	const QJsonObject replacement = createMatrixTextMessagePayload(body, metadata).content;
	QJsonObject content = replacement;
	content.insert(QStringLiteral("body"), QStringLiteral("* ") + body);
	content.insert(QStringLiteral("formatted_body"), QStringLiteral("* ") +
		replacement.value(QStringLiteral("formatted_body")).toString());
	content.insert(QStringLiteral("m.new_content"), replacement);
	content.insert(QStringLiteral("m.relates_to"), QJsonObject{
		{QStringLiteral("rel_type"), QStringLiteral("m.replace")},
		{QStringLiteral("event_id"), originalEventId}});
	return content;
}

MatrixEncryptedRoomEventPayload splitMatrixEncryptedRoomEventContent(
	const QString &roomId, const QString &eventType, const QJsonObject &content)
{
	QJsonObject encryptedContent = content;
	QJsonObject outerContent;
	const QJsonObject relation = encryptedContent.value(QStringLiteral("m.relates_to")).toObject();
	if (!relation.isEmpty()) {
		encryptedContent.remove(QStringLiteral("m.relates_to"));
		outerContent.insert(QStringLiteral("m.relates_to"), relation);
	}

	return {
		QJsonObject{{QStringLiteral("room_id"), roomId},
			{QStringLiteral("type"), eventType},
			{QStringLiteral("content"), encryptedContent}},
		outerContent};
}

QJsonObject matrixRelationForEvent(const QJsonObject &outerContent,
	const QJsonObject &decryptedContent)
{
	const QJsonObject clearRelation = outerContent.value(QStringLiteral("m.relates_to")).toObject();
	return clearRelation.isEmpty()
		? decryptedContent.value(QStringLiteral("m.relates_to")).toObject()
		: clearRelation;
}
