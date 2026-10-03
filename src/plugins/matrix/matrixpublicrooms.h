#ifndef MATRIXPUBLICROOMS_H
#define MATRIXPUBLICROOMS_H

#include <QList>
#include <QString>
#include <QMetaType>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrl>

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

#endif // MATRIXPUBLICROOMS_H
