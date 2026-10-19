#include "matrixroomapi.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

namespace {

QNetworkRequest buildAuthenticatedRequest(const QUrl &endpoint, const QByteArray &accessToken, bool allowHttp2)
{
    QNetworkRequest request(endpoint);
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, allowHttp2);
    request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + accessToken.trimmed());
    return request;
}

bool validateEndpointAndToken(QNetworkAccessManager *manager, const QUrl &endpoint,
    const QByteArray &accessToken)
{
    return manager && endpoint.isValid() &&
        (endpoint.scheme() == QStringLiteral("http") || endpoint.scheme() == QStringLiteral("https")) &&
        !accessToken.trimmed().isEmpty();
}

} // namespace

namespace MatrixPublicRooms {

QNetworkReply *requestPublicRooms(QNetworkAccessManager *manager, const QUrl &endpoint,
    const QByteArray &accessToken, const QString &directoryServer,
    const QString &searchTerm, int limit, const QString &since, bool allowHttp2)
{
    if (!validateEndpointAndToken(manager, endpoint, accessToken))
        return nullptr;

    QUrl url(endpoint);
    QUrlQuery query(url);
    const QString server = directoryServer.trimmed();
    if (!server.isEmpty())
        query.addQueryItem(QStringLiteral("server"), server);
    const QString term = searchTerm.trimmed();
    if (!term.isEmpty()) {
        const QJsonObject filter{{QStringLiteral("generic_search_term"), term}};
        query.addQueryItem(QStringLiteral("filter"),
            QString::fromUtf8(QJsonDocument(filter).toJson(QJsonDocument::Compact)));
    }
    if (limit > 0)
        query.addQueryItem(QStringLiteral("limit"), QString::number(limit));
    if (!since.isEmpty())
        query.addQueryItem(QStringLiteral("since"), since);
    url.setQuery(query);

    QNetworkRequest request = buildAuthenticatedRequest(url, accessToken, allowHttp2);
    return manager->get(request);
}

Result parsePublicRoomsResponse(const QByteArray &responseBody)
{
    Result result;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(responseBody, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.error = QStringLiteral("Invalid Matrix public-room response JSON");
        return result;
    }

    const QJsonObject response = document.object();
    const QJsonValue chunkValue = response.value(QStringLiteral("chunk"));
    if (!chunkValue.isArray()) {
        result.error = QStringLiteral("Matrix public-room response has no chunk array");
        return result;
    }

    result.nextBatch = response.value(QStringLiteral("next_batch")).toString();
    result.previousBatch = response.value(QStringLiteral("prev_batch")).toString();
    for (const QJsonValue &value : chunkValue.toArray()) {
        if (!value.isObject())
            continue;
        const QJsonObject item = value.toObject();
        Room room;
        room.roomId = item.value(QStringLiteral("room_id")).toString();
        if (room.roomId.isEmpty())
            continue;
        room.canonicalAlias = item.value(QStringLiteral("canonical_alias")).toString();
        room.name = item.value(QStringLiteral("name")).toString();
        room.topic = item.value(QStringLiteral("topic")).toString();
        room.avatarUrl = item.value(QStringLiteral("avatar_url")).toString();
        room.joinedMemberCount = item.value(QStringLiteral("num_joined_members")).toInt();
        room.worldReadable = item.value(QStringLiteral("world_readable")).toBool();
        room.guestCanJoin = item.value(QStringLiteral("guest_can_join")).toBool();
        result.rooms.append(room);
    }
    return result;
}

} // namespace MatrixPublicRooms

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
    if (!validateEndpointAndToken(manager, endpoint, accessToken) || !isValidUserId(inviteeUserId))
        return nullptr;

    QNetworkRequest request = buildAuthenticatedRequest(endpoint, accessToken, allowHttp2);
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
    if (!rooms.contains(QJsonValue(roomId))) {
        rooms.append(roomId);
        updated.insert(userId, rooms);
    }
    return updated;
}

} // namespace MatrixDirectRoom

namespace MatrixRoomInvite {

bool isValidRequestTarget(const QString &roomId, const QString &userId)
{
    return roomId.startsWith(QLatin1Char('!')) &&
        roomId.contains(QLatin1Char(':')) &&
        !roomId.contains(QRegularExpression(QStringLiteral("\\s"),
            QRegularExpression::UseUnicodePropertiesOption)) &&
        MatrixDirectRoom::isValidUserId(userId);
}

bool buildRequest(const QString &roomId, const QString &userId, Request &request)
{
    request = Request();
    if (!isValidRequestTarget(roomId, userId))
        return false;

    const QString encodedRoomId = QString::fromLatin1(QUrl::toPercentEncoding(roomId));
    request.path = QStringLiteral("/_matrix/client/v3/rooms/%1/invite").arg(encodedRoomId);
    request.body = QJsonDocument(QJsonObject{{QStringLiteral("user_id"), userId}})
        .toJson(QJsonDocument::Compact);
    return true;
}

} // namespace MatrixRoomInvite
