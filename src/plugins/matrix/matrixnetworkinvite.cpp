#include "matrixnetwork.h"

#include "matrixroominvite.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrl>

void MatrixNetwork::inviteUserToRoom(const QString &roomId, const QString &userId)
{
    MatrixRoomInvite::Request inviteRequest;
    if (!FNetworkAccessManager || FAccesToken.isEmpty() ||
        !MatrixRoomInvite::buildRequest(roomId, userId, inviteRequest)) {
        emit sendError(QStringLiteral("Cannot invite a Matrix user to this room: invalid room, user, or session"));
        return;
    }

    QNetworkRequest request(QUrl(constructUrl(inviteRequest.path)));
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, FUseHttp2);
    request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + FAccesToken.toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QNetworkReply *reply = FNetworkAccessManager->post(request, inviteRequest.body);
    reply->setProperty("requestType", QStringLiteral("room_invite"));
    reply->setProperty("roomId", roomId);
    reply->setProperty("inviteeUserId", userId);
    connect(reply, &QNetworkReply::finished, this, [this, reply, roomId, userId]() {
        if (reply->error() != QNetworkReply::NoError) {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
            const QString serverError = response.value(QStringLiteral("error")).toString();
            const QString detail = serverError.isEmpty() ? reply->errorString() : serverError;
            const QString errorMessage = QStringLiteral("Matrix invite for %1 failed (HTTP %2): %3")
                .arg(userId).arg(status).arg(detail);
            qWarning().noquote() << errorMessage;
            emit sendError(errorMessage);
        } else {
            qInfo() << "Matrix invite request accepted" << roomId << userId;
            sync();
        }
        reply->deleteLater();
    });
}
