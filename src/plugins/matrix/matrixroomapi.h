#ifndef MATRIXROOMAPI_H
#define MATRIXROOMAPI_H

#include <QByteArray>
#include <QList>
#include <QMetaType>
#include <QString>

class QJsonObject;
class QNetworkAccessManager;
class QNetworkReply;
class QUrl;

namespace MatrixRoomInvite {

struct Request
{
    QString path;
    QByteArray body;
};

bool isValidRequestTarget(const QString &roomId, const QString &userId);
bool buildRequest(const QString &roomId, const QString &userId, Request &request);

} // namespace MatrixRoomInvite

namespace MatrixPublicRooms {

struct Room
{
    QString roomId;
    QString canonicalAlias;
    QString name;
    QString topic;
    QString avatarUrl;
    int joinedMemberCount = 0;
    bool worldReadable = false;
    bool guestCanJoin = false;
};

struct Result
{
    QList<Room> rooms;
    QString nextBatch;
    QString previousBatch;
    QString error;

    bool succeeded() const { return error.isEmpty(); }
};

QNetworkReply *requestPublicRooms(QNetworkAccessManager *manager, const QUrl &endpoint,
    const QByteArray &accessToken, const QString &directoryServer,
    const QString &searchTerm, int limit, const QString &since, bool allowHttp2);
Result parsePublicRoomsResponse(const QByteArray &responseBody);

} // namespace MatrixPublicRooms

Q_DECLARE_METATYPE(MatrixPublicRooms::Result)

namespace MatrixDirectRoom {

bool isValidUserId(const QString &userId);
QNetworkReply *requestCreation(QNetworkAccessManager *manager, const QUrl &endpoint,
    const QByteArray &accessToken, const QString &inviteeUserId, bool allowHttp2,
    bool enableEncryption = false);
QString parseCreatedRoomId(const QByteArray &responseBody, QString &error);
QJsonObject addRoomToDirectMapping(const QJsonObject &mapping,
    const QString &userId, const QString &roomId);

} // namespace MatrixDirectRoom

#endif // MATRIXROOMAPI_H