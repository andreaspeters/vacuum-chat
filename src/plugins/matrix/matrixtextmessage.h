#ifndef MATRIXTEXTMESSAGE_H
#define MATRIXTEXTMESSAGE_H

#include <QJsonObject>
#include <QString>
#include <QVariantMap>

struct MatrixTextMessagePayload
{
	QJsonObject content;
	QVariantMap localEchoMetadata;
};

struct MatrixEncryptedRoomEventPayload
{
	QJsonObject plaintextEvent;
	QJsonObject outerContent;
};

QString matrixMessageTypeForMetadata(const QVariantMap &metadata);

MatrixTextMessagePayload createMatrixTextMessagePayload(
	const QString &body, const QVariantMap &metadata);

QJsonObject createMatrixEditMessageContent(const QString &body,
	const QVariantMap &metadata, const QString &originalEventId);

MatrixEncryptedRoomEventPayload splitMatrixEncryptedRoomEventContent(
	const QString &roomId, const QString &eventType, const QJsonObject &content);
QJsonObject matrixRelationForEvent(const QJsonObject &outerContent,
	const QJsonObject &decryptedContent);

#endif // MATRIXTEXTMESSAGE_H
