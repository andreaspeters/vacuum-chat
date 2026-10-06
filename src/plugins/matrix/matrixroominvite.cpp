#include "matrixroominvite.h"

#include "matrixdirectroom.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

namespace MatrixRoomInvite {

bool buildRequest(const QString &roomId, const QString &userId, Request &request)
{
    request = Request();
    bool hasWhitespace = false;
    for (const QChar character : roomId)
        hasWhitespace = hasWhitespace || character.isSpace();
    if (roomId != roomId.trimmed() || !roomId.startsWith(QLatin1Char('!')) ||
        !roomId.contains(QLatin1Char(':')) || hasWhitespace ||
        !MatrixDirectRoom::isValidUserId(userId))
        return false;

    const QString encodedRoomId = QString::fromLatin1(QUrl::toPercentEncoding(roomId));
    request.path = QStringLiteral("/_matrix/client/v3/rooms/%1/invite").arg(encodedRoomId);
    request.body = QJsonDocument(QJsonObject{{QStringLiteral("user_id"), userId}})
        .toJson(QJsonDocument::Compact);
    return true;
}

} // namespace MatrixRoomInvite
