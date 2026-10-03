#include "matrixdirectroom.h"

#include <QCoreApplication>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QEventLoop>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <iostream>

namespace {
int fail(const char *message)
{
    std::cerr << message << '\n';
    return 1;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0))
        return fail("local HTTP fixture did not listen");

    QByteArray requestBytes;
    bool responded = false;
    QObject::connect(&server, &QTcpServer::newConnection, &app, [&]() {
        QTcpSocket *socket = server.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, &app, [&, socket]() {
            requestBytes += socket->readAll();
            if (responded || !requestBytes.contains("\r\n\r\n"))
                return;
            responded = true;
            const QByteArray body = R"({"room_id":"!new:example.org"})";
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
    const QUrl endpoint(QStringLiteral("http://127.0.0.1:%1/_matrix/client/v3/createRoom")
        .arg(server.serverPort()));
    QNetworkReply *reply = MatrixDirectRoom::requestCreation(&manager, endpoint,
        QByteArrayLiteral("synthetic-test-token"), QStringLiteral("@newuser:example.org"), false, false);
    if (!reply)
        return fail("direct-room creation returned no reply for a valid Matrix user ID");

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(5000);
    loop.exec();
    if (!responded || reply->isRunning() || reply->error() != QNetworkReply::NoError)
        return fail("direct-room creation did not complete against the local fixture");
    if (!requestBytes.startsWith("POST /_matrix/client/v3/createRoom ") ||
        !requestBytes.contains("Authorization: Bearer synthetic-test-token"))
        return fail("direct-room request used the wrong method, path, or authorization");

    const QByteArray body = requestBytes.mid(requestBytes.indexOf("\r\n\r\n") + 4);
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
