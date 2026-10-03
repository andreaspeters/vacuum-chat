#include "matrixpublicrooms.h"

#include <QCoreApplication>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QEventLoop>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>

#include <iostream>

namespace {

bool containsHeader(const QByteArray &request, const QByteArray &header)
{
    for (const QByteArray &line : request.split('\n'))
        if (line.trimmed().toLower() == header.toLower())
            return true;
    return false;
}

int fail(const char *message)
{
    std::cerr << message << '\n';
    return 1;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0))
        return fail("local HTTP fixture did not listen");

    QByteArray capturedRequest;
    bool responseSent = false;
    QObject::connect(&server, &QTcpServer::newConnection, &app, [&]() {
        QTcpSocket *socket = server.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, &app, [&, socket]() {
            capturedRequest += socket->readAll();
            if (responseSent || !capturedRequest.contains("\r\n\r\n"))
                return;
            responseSent = true;
            const QByteArray body = R"({"chunk":[{"room_id":"!lobby:remote.example","canonical_alias":"#lobby:remote.example","name":"Lobby","topic":"Public discussion","avatar_url":"mxc://remote.example/avatar","num_joined_members":17,"world_readable":true,"guest_can_join":false}],"next_batch":"cursor+/=","prev_batch":"older"})";
            QByteArray response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ";
            response += QByteArray::number(body.size());
            response += "\r\nConnection: close\r\n\r\n";
            response += body;
            socket->write(response);
            socket->flush();
            socket->disconnectFromHost();
        });
    });

    QNetworkAccessManager manager;
    const QUrl endpoint(QStringLiteral("http://127.0.0.1:%1/_matrix/client/v3/publicRooms")
        .arg(server.serverPort()));
    QNetworkReply *reply = MatrixPublicRooms::requestPublicRooms(&manager, endpoint,
        QByteArrayLiteral("synthetic-test-token"), QStringLiteral("directory.example:8448"),
        QStringLiteral("classic chat"), 25, QStringLiteral("page token+1"), false);
    if (!reply)
        return fail("public-room request helper returned no reply");

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(5000);
    loop.exec();
    if (!responseSent || reply->isRunning())
        return fail("local HTTP fixture timed out");
    if (reply->error() != QNetworkReply::NoError)
        return fail("public-room request failed against local HTTP fixture");
    if (!capturedRequest.startsWith("GET /_matrix/client/v3/publicRooms?"))
        return fail("request did not use the Matrix v3 publicRooms GET endpoint");
    if (!containsHeader(capturedRequest, "Authorization: Bearer synthetic-test-token"))
        return fail("request omitted its bearer token");

    const QByteArray requestTarget = capturedRequest.split('\n').first().split(' ').value(1);
    const QUrl requestUrl = QUrl::fromEncoded("http://localhost" + requestTarget);
    const QUrlQuery query(requestUrl);
    if (query.queryItemValue(QStringLiteral("server")) != QStringLiteral("directory.example:8448") ||
        query.queryItemValue(QStringLiteral("limit")) != QStringLiteral("25") ||
        query.queryItemValue(QStringLiteral("since")) != QStringLiteral("page token+1"))
        return fail("public-room request query omitted server, limit, or pagination cursor");
    const QJsonDocument filter = QJsonDocument::fromJson(
        query.queryItemValue(QStringLiteral("filter")).toUtf8());
    if (!filter.isObject() || filter.object().value(QStringLiteral("generic_search_term")).toString()
        != QStringLiteral("classic chat"))
        return fail("public-room request omitted its generic_search_term filter");

    const MatrixPublicRooms::Result result = MatrixPublicRooms::parsePublicRoomsResponse(reply->readAll());
    reply->deleteLater();
    if (!result.succeeded() || result.rooms.size() != 1 ||
        result.nextBatch != QStringLiteral("cursor+/=") ||
        result.previousBatch != QStringLiteral("older"))
        return fail("public-room response pagination or room list was not parsed");
    const MatrixPublicRooms::Room &room = result.rooms.first();
    if (room.roomId != QStringLiteral("!lobby:remote.example") ||
        room.canonicalAlias != QStringLiteral("#lobby:remote.example") ||
        room.name != QStringLiteral("Lobby") || room.topic != QStringLiteral("Public discussion") ||
        room.avatarUrl != QStringLiteral("mxc://remote.example/avatar") ||
        room.joinedMemberCount != 17 || !room.worldReadable || room.guestCanJoin)
        return fail("public-room result fields were not parsed correctly");

    const MatrixPublicRooms::Result malformed = MatrixPublicRooms::parsePublicRoomsResponse(
        QByteArrayLiteral("not-json"));
    if (malformed.succeeded() || !malformed.rooms.isEmpty())
        return fail("malformed public-room response was accepted");

    std::cout << "Matrix public-room HTTP and parsing test passed\n";
    return 0;
}
