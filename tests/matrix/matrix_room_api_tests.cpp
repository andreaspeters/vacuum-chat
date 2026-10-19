#include "matrixroomapi.h"

#include <QCoreApplication>
#include <QHostAddress>
#include <QJsonArray>
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

void waitForReply(QNetworkReply *reply)
{
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(5000);
    loop.exec();
}

struct LocalHttpFixture
{
    QByteArray request;
    bool responseSent = false;
    QTcpServer server;

    bool start(const QByteArray &body)
    {
        if (!server.listen(QHostAddress::LocalHost, 0))
            return false;
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this, body]() {
            QTcpSocket *socket = server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::readyRead, &server, [this, socket, body]() {
                request += socket->readAll();
                if (responseSent || !request.contains("\r\n\r\n"))
                    return;
                responseSent = true;
                QByteArray response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ";
                response += QByteArray::number(body.size());
                response += "\r\nConnection: close\r\n\r\n";
                response += body;
                socket->write(response);
                socket->flush();
                socket->disconnectFromHost();
            });
        });
        return true;
    }
};

int testPublicRooms()
{
    const QByteArray responseBody =
        R"({"chunk":[{"room_id":"!lobby:remote.example",)"
        R"("canonical_alias":"#lobby:remote.example","name":"Lobby",)"
        R"("topic":"Public discussion","avatar_url":"mxc://remote.example/avatar",)"
        R"("num_joined_members":17,"world_readable":true,"guest_can_join":false}],)"
        R"("next_batch":"cursor+/=","prev_batch":"older"})";
    LocalHttpFixture fixture;
    if (!fixture.start(responseBody))
        return fail("local HTTP fixture did not listen");

    QNetworkAccessManager manager;
    const QUrl endpoint(QStringLiteral("http://127.0.0.1:%1/_matrix/client/v3/publicRooms")
        .arg(fixture.server.serverPort()));
    QNetworkReply *reply = MatrixPublicRooms::requestPublicRooms(&manager, endpoint,
        QByteArrayLiteral("synthetic-test-token"), QStringLiteral("directory.example:8448"),
        QStringLiteral("classic chat"), 25, QStringLiteral("page token+1"), false);
    if (!reply)
        return fail("public-room request helper returned no reply");

    waitForReply(reply);
    if (!fixture.responseSent || reply->isRunning())
        return fail("local HTTP fixture timed out");
    if (reply->error() != QNetworkReply::NoError)
        return fail("public-room request failed against local HTTP fixture");
    if (!fixture.request.startsWith("GET /_matrix/client/v3/publicRooms?"))
        return fail("request did not use the Matrix v3 publicRooms GET endpoint");
    if (!containsHeader(fixture.request,
            QByteArrayLiteral("Authorization: Bearer ") + QByteArrayLiteral("synthetic-test-token")))
        return fail("request omitted its bearer token");

    const QByteArray requestTarget = fixture.request.split('\n').first().split(' ').value(1);
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

int testDirectRoom()
{
    const QByteArray responseBody = R"({"room_id":"!new:example.org"})";
    LocalHttpFixture fixture;
    if (!fixture.start(responseBody))
        return fail("local HTTP fixture did not listen");

    QNetworkAccessManager manager;
    const QUrl endpoint(QStringLiteral("http://127.0.0.1:%1/_matrix/client/v3/createRoom")
        .arg(fixture.server.serverPort()));
    QNetworkReply *reply = MatrixDirectRoom::requestCreation(&manager, endpoint,
        QByteArrayLiteral("synthetic-test-token"), QStringLiteral("@newuser:example.org"), false, false);
    if (!reply)
        return fail("direct-room creation returned no reply for a valid Matrix user ID");

    waitForReply(reply);
    if (!fixture.responseSent || reply->isRunning() || reply->error() != QNetworkReply::NoError)
        return fail("direct-room creation did not complete against the local fixture");
    if (!fixture.request.startsWith("POST /_matrix/client/v3/createRoom ") ||
        !fixture.request.contains(
            QByteArrayLiteral("Authorization: Bearer ") + QByteArrayLiteral("synthetic-test-token")))
        return fail("direct-room request used the wrong method, path, or authorization");

    const QByteArray body = fixture.request.mid(fixture.request.indexOf("\r\n\r\n") + 4);
    const QJsonObject payload = QJsonDocument::fromJson(body).object();
    const QJsonArray invite = payload.value(QStringLiteral("invite")).toArray();
    const QJsonArray initialState = payload.value(QStringLiteral("initial_state")).toArray();
    if (!payload.value(QStringLiteral("is_direct")).toBool() ||
        payload.value(QStringLiteral("preset")).toString() != QStringLiteral("trusted_private_chat") ||
        invite.size() != 1 || invite.first().toString() != QStringLiteral("@newuser:example.org") ||
        !initialState.isEmpty())
        return fail("unencrypted direct-room creation payload omitted intent or invite");

    QString parseError;
    if (MatrixDirectRoom::parseCreatedRoomId(reply->readAll(), parseError) !=
        QStringLiteral("!new:example.org") || !parseError.isEmpty())
        return fail("created room ID response was not parsed");
    reply->deleteLater();

    if (MatrixDirectRoom::requestCreation(&manager, endpoint, QByteArrayLiteral("token"),
        QStringLiteral("not-a-mxid"), false) != nullptr)
        return fail("invalid Matrix user ID was sent to the homeserver");

    const QJsonObject original{{QStringLiteral("@other:example.org"),
            QJsonArray{QStringLiteral("!other:example.org")}},
        {QStringLiteral("@newuser:example.org"), QJsonArray{QStringLiteral("!old:example.org")}}};
    const QJsonObject updated = MatrixDirectRoom::addRoomToDirectMapping(original,
        QStringLiteral("@newuser:example.org"), QStringLiteral("!new:example.org"));
    const QJsonArray updatedUserRooms = updated.value(QStringLiteral("@newuser:example.org")).toArray();
    if (updated.value(QStringLiteral("@other:example.org")) != original.value(QStringLiteral("@other:example.org")) ||
        updatedUserRooms.size() != 2 || updatedUserRooms.last().toString() != QStringLiteral("!new:example.org"))
        return fail("m.direct update did not preserve existing users and rooms");
    if (MatrixDirectRoom::addRoomToDirectMapping(updated, QStringLiteral("@newuser:example.org"),
        QStringLiteral("!new:example.org")) != updated)
        return fail("m.direct update added a duplicate room ID");

    std::cout << "Matrix direct-room request and account-data test passed\n";
    return 0;
}

int testRoomInvite()
{
    const QString roomId = QStringLiteral("!room:example.org");
    const QString userId = QStringLiteral("@invitee:example.org");
    MatrixRoomInvite::Request request;
    if (!MatrixRoomInvite::isValidRequestTarget(roomId, userId) ||
        !MatrixRoomInvite::buildRequest(roomId, userId, request))
        return fail("valid Matrix room invite request was rejected");
    if (request.path != QStringLiteral("/_matrix/client/v3/rooms/%21room%3Aexample.org/invite"))
        return fail("Matrix room invite request used the wrong endpoint path");
    const QJsonObject body = QJsonDocument::fromJson(request.body).object();
    if (body.value(QStringLiteral("user_id")).toString() != userId || body.size() != 1)
        return fail("Matrix room invite request has the wrong JSON body");

    MatrixRoomInvite::Request invalid;
    if (MatrixRoomInvite::isValidRequestTarget(QStringLiteral("room:example.org"), userId) ||
        MatrixRoomInvite::buildRequest(QStringLiteral("room:example.org"), userId, invalid))
        return fail("invite request accepted a room alias in place of a room ID");
    if (MatrixRoomInvite::isValidRequestTarget(QStringLiteral("!room with spaces:example.org"), userId) ||
        MatrixRoomInvite::buildRequest(QStringLiteral("!room with spaces:example.org"), userId, invalid))
        return fail("invite request accepted whitespace in a room ID");
    const QString unicodeWhitespaceRoom = QStringLiteral("!room:\u00a0example.org");
    if (MatrixRoomInvite::isValidRequestTarget(unicodeWhitespaceRoom, userId) ||
        MatrixRoomInvite::buildRequest(unicodeWhitespaceRoom, userId, invalid))
        return fail("invite request accepted Unicode whitespace in a room ID");
    if (MatrixRoomInvite::isValidRequestTarget(roomId, QStringLiteral("not-a-user-id")) ||
        MatrixRoomInvite::buildRequest(roomId, QStringLiteral("not-a-user-id"), invalid))
        return fail("invite request accepted a malformed Matrix user ID");

    std::cout << "Matrix room-invite request test passed\n";
    return 0;
}

} // namespace

int runMatrixPolicyTests();

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    
    int exitCode = runMatrixPolicyTests() == 0 ? 0 : 1;
    if (testPublicRooms() != 0)
        exitCode = 1;
    if (testDirectRoom() != 0)
        exitCode = 1;
    if (testRoomInvite() != 0)
        exitCode = 1;

    return exitCode;
}