#ifndef MATRIXDIRECTROOM_H
#define MATRIXDIRECTROOM_H

#include <QByteArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>
#include <QUrl>

namespace MatrixDirectRoom {

bool isValidUserId(const QString &userId);
QNetworkReply *requestCreation(QNetworkAccessManager *manager, const QUrl &endpoint,
	const QByteArray &accessToken, const QString &inviteeUserId, bool allowHttp2,
	bool enableEncryption = false);
QString parseCreatedRoomId(const QByteArray &responseBody, QString &error);
QJsonObject addRoomToDirectMapping(const QJsonObject &mapping,
    const QString &userId, const QString &roomId);

} // namespace MatrixDirectRoom

#endif // MATRIXDIRECTROOM_H
