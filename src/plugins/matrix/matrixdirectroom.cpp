#include "matrixdirectroom.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkRequest>

namespace MatrixDirectRoom {
namespace {

QJsonObject directRoomCreationPayload(const QString &inviteeUserId, bool enableEncryption)
{
	QJsonObject payload{{QStringLiteral("preset"), QStringLiteral("trusted_private_chat")},
		{QStringLiteral("is_direct"), true},
		{QStringLiteral("invite"), QJsonArray{inviteeUserId}}};
	if (enableEncryption) {
		const QJsonObject encryptionContent{{QStringLiteral("algorithm"),
			QStringLiteral("m.megolm.v1.aes-sha2")}};
		const QJsonObject encryptionState{{QStringLiteral("type"), QStringLiteral("m.room.encryption")},
			{QStringLiteral("state_key"), QString()}, {QStringLiteral("content"), encryptionContent}};
		payload.insert(QStringLiteral("initial_state"), QJsonArray{encryptionState});
	}
	return payload;
}

} // namespace

bool isValidUserId(const QString &userId)
{
    const QString value = userId.trimmed();
    const qsizetype separator = value.indexOf(QLatin1Char(':'));
    return value == userId && value.startsWith(QLatin1Char('@')) && separator > 1 &&
        separator < value.size() - 1 &&
        !value.contains(QRegularExpression(QStringLiteral("\\s")));
}

QNetworkReply *requestCreation(QNetworkAccessManager *manager, const QUrl &endpoint,
	const QByteArray &accessToken, const QString &inviteeUserId, bool allowHttp2,
	bool enableEncryption)
{
    if (!manager || !endpoint.isValid() ||
        (endpoint.scheme() != QStringLiteral("http") && endpoint.scheme() != QStringLiteral("https")) ||
        accessToken.trimmed().isEmpty() || !isValidUserId(inviteeUserId))
        return nullptr;

    QNetworkRequest request(endpoint);
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, allowHttp2);
    request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + accessToken.trimmed());
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    return manager->post(request, QJsonDocument(directRoomCreationPayload(inviteeUserId, enableEncryption))
        .toJson(QJsonDocument::Compact));
}

QString parseCreatedRoomId(const QByteArray &responseBody, QString &error)
{
    error.clear();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(responseBody, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        error = QStringLiteral("Invalid Matrix create-room response JSON");
        return QString();
    }
    const QString roomId = document.object().value(QStringLiteral("room_id")).toString();
    if (!roomId.startsWith(QLatin1Char('!')) || !roomId.contains(QLatin1Char(':'))) {
        error = QStringLiteral("Matrix create-room response has no valid room_id");
        return QString();
    }
    return roomId;
}

QJsonObject addRoomToDirectMapping(const QJsonObject &mapping,
    const QString &userId, const QString &roomId)
{
    if (!isValidUserId(userId) || roomId.isEmpty())
        return mapping;
    QJsonObject updated = mapping;
    QJsonArray rooms = updated.value(userId).toArray();
    for (const QJsonValue &existing : rooms)
        if (existing.toString() == roomId)
            return updated;
    rooms.append(roomId);
    updated.insert(userId, rooms);
    return updated;
}

} // namespace MatrixDirectRoom
