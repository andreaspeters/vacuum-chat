#include "matrixpublicrooms.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkRequest>
#include <QUrlQuery>

namespace MatrixPublicRooms {

QNetworkReply *requestPublicRooms(QNetworkAccessManager *manager, const QUrl &endpoint,
    const QByteArray &accessToken, const QString &directoryServer,
    const QString &searchTerm, int limit, const QString &since, bool allowHttp2)
{
    if (!manager || !endpoint.isValid() ||
        (endpoint.scheme() != QStringLiteral("http") && endpoint.scheme() != QStringLiteral("https")) ||
        accessToken.trimmed().isEmpty())
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

    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, allowHttp2);
    request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + accessToken.trimmed());
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
