#include "matrixdatabaseworker.h"
#include "matrixnetwork.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QNetworkReply>
#include <QEventLoop>
#include <QHostAddress>
#include <QTemporaryDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>

#include <cstring>
#include <iostream>

class MatrixNetworkTestAccess
{
public:
    static void processSyncResponse(MatrixNetwork &network, QNetworkReply *reply)
    {
        network.onSyncFinished(reply);
    }

    static void setAccessToken(MatrixNetwork &network, const QString &token)
    {
        network.FAccesToken = token;
    }

    static void setUserId(MatrixNetwork &network, const QString &userId)
    {
        network.FUserId = userId;
    }

    static void setRedactionIdentity(MatrixNetwork &network, const QString &userId)
    {
        network.FUserId = userId;
        network.FActiveLoginGeneration = 7;
    }

    static bool roomIsEncrypted(const MatrixNetwork &network, const QString &roomId)
    {
        return network.FRooms.value(roomId).isEncrypted;
    }

    static bool roomEncryptionStateKnown(const MatrixNetwork &network, const QString &roomId)
    {
        return network.FRooms.value(roomId).encryptionStateKnown;
    }

    static bool roomExists(const MatrixNetwork &network, const QString &roomId)
    {
        return network.FRooms.contains(roomId);
    }

    static void addUnknownJoinedRoom(MatrixNetwork &network, const QString &roomId)
    {
        ProtocolRoom room;
        room.id = roomId;
        room.name = roomId;
        room.membership = QStringLiteral("join");
        room.isJoined = true;
        room.isAvailable = true;
        network.FRooms.insert(roomId, room);
    }

    static void addDirectRoom(MatrixNetwork &network, const QString &roomId,
        const QStringList &memberIds, bool isDirect, bool isJoined)
    {
        ProtocolRoom room;
        room.id = roomId;
        room.name = roomId;
        room.membership = isJoined ? QStringLiteral("join") : QStringLiteral("leave");
        room.isJoined = isJoined;
        room.isAvailable = true;
        room.isDirect = isDirect;
        for (const QString &memberId : memberIds) {
            ProtocolRosterEntry member;
            member.id = memberId;
            member.isValid = true;
            room.members.append(member);
        }
        network.FRooms.insert(roomId, room);
        if (isDirect) {
            network.FDirectRoomIds.insert(roomId);
            network.FHasDirectRoomData = true;
        }
    }

    static void suppressBackgroundSync(MatrixNetwork &network)
    {
        network.FSyncInFlight = true;
    }

    static void restorePersistedRooms(MatrixNetwork &network)
    {
        network.restorePersistedRooms();
    }
};

namespace {

class SyntheticSyncReply final : public QNetworkReply
{
public:
    explicit SyntheticSyncReply(const QByteArray &body, QObject *parent = nullptr)
        : QNetworkReply(parent), FBody(body)
    {
        const QNetworkRequest request(QUrl(QStringLiteral("https://matrix.example/_matrix/client/v3/sync")));
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
        open(QIODevice::ReadOnly);
        setFinished(true);
    }

    void abort() override {}
    bool isSequential() const override { return true; }

protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        if (FOffset >= FBody.size())
            return -1;
        const qint64 size = qMin(maxSize, static_cast<qint64>(FBody.size()) - FOffset);
        std::memcpy(data, FBody.constData() + FOffset, static_cast<size_t>(size));
        FOffset += size;
        return size;
    }

private:
    QByteArray FBody;
    qint64 FOffset = 0;
};

bool readStoredEncryption(MatrixDatabaseWorker *worker, const QString &roomId, bool &encrypted,
    bool &encryptionStateKnown)
{
    bool found = false;
    QMetaObject::invokeMethod(worker, [worker, roomId, &found, &encrypted, &encryptionStateKnown]() {
        worker->execute([roomId, &found, &encrypted, &encryptionStateKnown](MatrixDatabase &database) {
            for (const MatrixStoredRoom &room : database.roomStates()) {
                if (room.roomId == roomId) {
                    found = true;
                    encrypted = room.isEncrypted;
                    encryptionStateKnown = room.encryptionStateKnown;
                    break;
                }
            }
        });
    }, Qt::BlockingQueuedConnection);
    return found;
}

bool testRoomEncryptionLookup(MatrixNetwork &network, const QString &roomId, bool encryptedState)
{
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0))
        return false;
    const bool startedUnknown = MatrixNetworkTestAccess::roomExists(network, roomId) &&
        !MatrixNetworkTestAccess::roomEncryptionStateKnown(network, roomId);
    int requestCount = 0;
    int stateRequests = 0;
    int outboundEvents = 0;
    bool sendAcknowledged = false;
    bool unexpectedRequest = false;
    QList<QByteArray> requestLines;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    const auto handleRequest = [&requestCount, &stateRequests, &outboundEvents, &unexpectedRequest,
        &requestLines, &loop, encryptedState](QTcpSocket *socket) {
        const QByteArray request = socket->readAll();
        if (request.isEmpty())
            return;
        ++requestCount;
        const QByteArray requestLine = request.left(request.indexOf(char(13)));
        requestLines.append(requestLine);
        QJsonObject responseObject;
        QByteArray statusLine;
        if (requestLine.startsWith("GET ") && requestLine.contains("/state/m.room.encryption/")) {
            ++stateRequests;
            if (encryptedState) {
                responseObject = QJsonObject{{QStringLiteral("algorithm"),
                    QStringLiteral("m.megolm.v1.aes-sha2")}};
                statusLine = QByteArrayLiteral("HTTP/1.1 200 OK\r\n");
            } else {
                responseObject = QJsonObject{{QStringLiteral("errcode"), QStringLiteral("M_NOT_FOUND")},
                    {QStringLiteral("error"), QStringLiteral("No encryption state")}};
                statusLine = QByteArrayLiteral("HTTP/1.1 404 Not Found\r\n");
            }
        } else if (requestLine.startsWith("PUT ") && requestLine.contains("/send/")) {
            ++outboundEvents;
            responseObject = QJsonObject{{QStringLiteral("event_id"), QStringLiteral("$synthetic-send")}};
            statusLine = QByteArrayLiteral("HTTP/1.1 200 OK\r\n");
        } else {
            unexpectedRequest = true;
            responseObject = QJsonObject{{QStringLiteral("errcode"), QStringLiteral("M_UNRECOGNIZED")}};
            statusLine = QByteArrayLiteral("HTTP/1.1 400 Bad Request\r\n");
        }
        const QByteArray body = QJsonDocument(responseObject).toJson(QJsonDocument::Compact);
        QByteArray response = statusLine + QByteArrayLiteral("Content-Type: application/json\r\nContent-Length: ");
        response += QByteArray::number(body.size());
        response += QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body;
        socket->write(response);
        socket->disconnectFromHost();
    };
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&server, handleRequest]() {
        while (server.hasPendingConnections()) {
            QTcpSocket *socket = server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::readyRead, socket,
                [socket, handleRequest]() { handleRequest(socket); });
            if (socket->bytesAvailable() > 0)
                handleRequest(socket);
        }
    });

    const QString originalServer = network.serverUrl();
    network.setServerUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
    MatrixNetworkTestAccess::setAccessToken(network, QStringLiteral("synthetic-token"));
    const auto resolvedConnection = QObject::connect(&network, &MatrixNetwork::rosterChanged,
        &loop, [&loop, &network, roomId, encryptedState, &outboundEvents]() {
            if (MatrixNetworkTestAccess::roomEncryptionStateKnown(network, roomId) &&
                (encryptedState || outboundEvents > 0))
                loop.quit();
        });
    const auto sentConnection = QObject::connect(&network, &MatrixNetwork::messageSent,
        &loop, [&loop, &sendAcknowledged](const QString &messageId) {
            if (messageId == QStringLiteral("$synthetic-send")) {
                sendAcknowledged = true;
                loop.quit();
            }
        });
    timeout.start(1500);
    network.sendTextMessage(roomId,
        QStringLiteral("synthetic plaintext"), QStringLiteral("txn-unknown-state"));
    loop.exec();
    QObject::disconnect(resolvedConnection);
    QObject::disconnect(sentConnection);

    MatrixNetworkTestAccess::setAccessToken(network, QString());
    network.setServerUrl(originalServer);
    const bool correctSend = encryptedState ? outboundEvents == 0 :
        outboundEvents == 1 && sendAcknowledged && requestLines.size() == 2 &&
            requestLines.last().contains("/send/m.room.message/txn-unknown-state");
    const bool requestedState = startedUnknown && stateRequests == 1 && correctSend && !unexpectedRequest &&
        MatrixNetworkTestAccess::roomEncryptionStateKnown(network, roomId) &&
        MatrixNetworkTestAccess::roomIsEncrypted(network, roomId) == encryptedState;
    if (!requestedState) {
        std::cerr << "state lookup requests=" << stateRequests << ", outbound events="
            << outboundEvents << ", total requests=" << requestCount << '\n';
        for (const QByteArray &line : requestLines)
            std::cerr << line.constData() << '\n';
    }
    return requestedState;
}

bool testRoomPowerLevelsLookup()
{
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0))
        return false;
    MatrixNetwork network;
    const QString roomId = QStringLiteral("!member-actions:example.org");
    MatrixNetworkTestAccess::addUnknownJoinedRoom(network, roomId);
    network.setServerUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
    MatrixNetworkTestAccess::setAccessToken(network, QStringLiteral("synthetic-token"));

    const QJsonObject expected{{QStringLiteral("users_default"), 0},
        {QStringLiteral("kick"), 50}, {QStringLiteral("ban"), 50}};
    QString receivedRoomId;
    QJsonObject receivedContent;
    int requestCount = 0;
    bool validRequest = false;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&network, &MatrixNetwork::roomPowerLevelsChanged, &loop,
        [&loop, &receivedRoomId, &receivedContent](const QString &receivedRoom,
            const QJsonObject &content) {
            receivedRoomId = receivedRoom;
            receivedContent = content;
            loop.quit();
        });
    const auto handleRequest = [&requestCount, &validRequest](QTcpSocket *socket) {
        const QByteArray request = socket->readAll();
        if (request.isEmpty())
            return;
        ++requestCount;
        const QByteArray requestLine = request.left(request.indexOf(char(13)));
        validRequest = requestLine.startsWith("GET ") &&
            requestLine.contains("/state/m.room.power_levels/") &&
            request.contains("Authorization: Bearer synthetic-token");
        const QByteArray body = QByteArrayLiteral(
            "{\"users_default\":0,\"kick\":50,\"ban\":50}");
        QByteArray response = QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ");
        response += QByteArray::number(body.size());
        response += QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body;
        socket->write(response);
        socket->disconnectFromHost();
    };
    QObject::connect(&server, &QTcpServer::newConnection, &server,
        [&server, handleRequest]() {
            while (server.hasPendingConnections()) {
                QTcpSocket *socket = server.nextPendingConnection();
                QObject::connect(socket, &QTcpSocket::readyRead, socket,
                    [socket, handleRequest]() { handleRequest(socket); });
                if (socket->bytesAvailable() > 0)
                    handleRequest(socket);
            }
        });

    timeout.start(1500);
    network.requestRoomPowerLevels(roomId);
    network.requestRoomPowerLevels(roomId); // Duplicate profile queries must coalesce.
    loop.exec();
    MatrixNetworkTestAccess::setAccessToken(network, QString());
    return receivedRoomId == roomId && receivedContent == expected &&
        requestCount == 1 && validRequest;
}

bool testRoomMemberModeration()
{
    const QString roomId = QStringLiteral("!moderation:example.org");
    const QString memberId = QStringLiteral("@member:example.org");
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0))
        return false;
    MatrixNetwork network;
    MatrixNetworkTestAccess::addUnknownJoinedRoom(network, roomId);
    network.setServerUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
    MatrixNetworkTestAccess::setAccessToken(network, QStringLiteral("moderation-test-token"));

    QList<QByteArray> requestLines;
    QList<QByteArray> requestHeaders;
    QList<QByteArray> requestBodies;
    QList<QPair<QString, QString>> failures;
    bool failureContextCorrect = true;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&network, &MatrixNetwork::roomMemberActionFailed, &loop,
        [&failures, &failureContextCorrect, &requestLines, &roomId, &memberId, &loop](
            const QString &failedRoom, const QString &failedMember, const QString &action,
            const QString &error) {
            failureContextCorrect = failureContextCorrect && failedRoom == roomId &&
                failedMember == memberId && !error.isEmpty();
            failures.append(qMakePair(action, error));
            if (failures.size() == 2 && requestLines.size() == 3)
                loop.quit();
        });
    const auto handleRequest = [&requestLines, &requestHeaders, &requestBodies, &failures, &loop](QTcpSocket *socket) {
        QByteArray request = socket->property("moderationRequestBuffer").toByteArray();
        request += socket->readAll();
        socket->setProperty("moderationRequestBuffer", request);
        const int headerEnd = request.indexOf(QByteArrayLiteral("\r\n\r\n"));
        if (headerEnd < 0)
            return;
        const QByteArray headers = request.left(headerEnd);
        int contentLength = 0;
        for (const QByteArray &line : headers.split('\n'))
            if (line.toLower().startsWith("content-length:"))
                contentLength = line.mid(line.indexOf(':') + 1).trimmed().toInt();
        if (request.size() < headerEnd + 4 + contentLength)
            return;
        const QByteArray requestLine = headers.left(headers.indexOf('\r'));
        if (requestLine.contains("/rooms/%21moderation%3Aexample.org/kick") ||
            requestLine.contains("/rooms/%21moderation%3Aexample.org/ban") ||
            requestLine.contains("/rooms/%21moderation%3Aexample.org/state/m.room.power_levels/")) {
            requestLines.append(requestLine);
            requestHeaders.append(headers);
            requestBodies.append(request.mid(headerEnd + 4, contentLength));
        }
        // Leave /sync unanswered: successful mutation handlers start a sync,
        // but it is outside the three requests under test.
        if (requestLine.contains("/sync"))
            return;
        const bool forbidden = requestLine.contains("/kick") || requestLine.contains("/state/m.room.power_levels/");
        const QByteArray responseBody = forbidden
            ? QByteArrayLiteral("{\"errcode\":\"M_FORBIDDEN\",\"error\":\"not allowed\"}")
            : QByteArrayLiteral("{}");
        QByteArray response = forbidden
            ? QByteArrayLiteral("HTTP/1.1 403 Forbidden\r\nContent-Type: application/json\r\nContent-Length: ")
            : QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ");
        response += QByteArray::number(responseBody.size());
        response += QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + responseBody;
        socket->write(response);
        socket->disconnectFromHost();
        if (requestLines.size() == 3 && failures.size() == 2)
            loop.quit();
    };
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&server, handleRequest]() {
        while (server.hasPendingConnections()) {
            QTcpSocket *socket = server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::readyRead, socket,
                [socket, handleRequest]() { handleRequest(socket); });
            if (socket->bytesAvailable() > 0)
                handleRequest(socket);
        }
    });

    QJsonObject expectedPowerLevels;
    expectedPowerLevels.insert(QStringLiteral("users_default"), 0);
    expectedPowerLevels.insert(QStringLiteral("state_default"), 50);
    QJsonObject users;
    users.insert(memberId, 50);
    expectedPowerLevels.insert(QStringLiteral("users"), users);
    network.changeRoomMember(roomId, memberId, QStringLiteral("kick"), QStringLiteral("kick reason"));
    network.changeRoomMember(roomId, memberId, QStringLiteral("ban"), QStringLiteral("ban reason"));
    network.updateRoomPowerLevels(roomId, memberId, expectedPowerLevels);
    timeout.start(3000);
    loop.exec();
    MatrixNetworkTestAccess::setAccessToken(network, QString());

    const QByteArray roomPath = QByteArrayLiteral("/rooms/%21moderation%3Aexample.org/");
    bool sawKick = false;
    bool sawBan = false;
    bool sawPowerUpdate = false;
    bool validAuthorization = requestHeaders.size() == 3;
    for (int i = 0; i < requestLines.size(); ++i) {
        validAuthorization = validAuthorization &&
            requestHeaders.at(i).contains("Authorization: Bearer moderation-test-token");
        const QByteArray &line = requestLines.at(i);
        const QJsonObject body = QJsonDocument::fromJson(requestBodies.at(i)).object();
        if (line.startsWith("POST ") && line.contains(roomPath + "kick"))
            sawKick = body.value(QStringLiteral("user_id")).toString() == memberId &&
                body.value(QStringLiteral("reason")).toString() == QStringLiteral("kick reason");
        else if (line.startsWith("POST ") && line.contains(roomPath + "ban"))
            sawBan = body.value(QStringLiteral("user_id")).toString() == memberId &&
                body.value(QStringLiteral("reason")).toString() == QStringLiteral("ban reason");
        else if (line.startsWith("PUT ") && line.contains(roomPath + "state/m.room.power_levels/"))
            sawPowerUpdate = body == expectedPowerLevels;
    }
    bool sawKickFailure = false;
    bool sawRoleFailure = false;
    for (const auto &failure : failures) {
        sawKickFailure = sawKickFailure || failure.first == QStringLiteral("kick");
        sawRoleFailure = sawRoleFailure || failure.first == QStringLiteral("role");
    }
    return requestLines.size() == 3 && sawKick && sawBan && sawPowerUpdate && validAuthorization &&
        failures.size() == 2 && sawKickFailure && sawRoleFailure && failureContextCorrect;
}

bool testRoomMemberRedaction(MatrixDatabaseWorker *worker)
{
    if (!worker)
        return false;
    const QString roomId = QStringLiteral("!redaction:example.org");
    const QString memberId = QStringLiteral("@member:example.org");
    QList<MatrixTimelineEvent> cachedEvents;
    for (const QString &eventId : {QStringLiteral("$redact-one:example.org"),
             QStringLiteral("$redact-two:example.org")}) {
        MatrixTimelineEvent event;
        event.roomId = roomId;
        event.eventId = eventId;
        event.eventType = QStringLiteral("m.room.message");
        event.sender = memberId;
        event.originTs = 1700000000000LL + cachedEvents.size();
        cachedEvents.append(event);
    }
    MatrixTimelineEvent unrelated;
    unrelated.roomId = roomId;
    unrelated.eventId = QStringLiteral("$other-sender:example.org");
    unrelated.eventType = QStringLiteral("m.room.message");
    unrelated.sender = QStringLiteral("@other:example.org");
    unrelated.originTs = 1700000000010LL;
    cachedEvents.append(unrelated);
    bool saved = false;
    const bool saveQueued = QMetaObject::invokeMethod(worker, [&]() {
        worker->execute([&](MatrixDatabase &database) {
            saved = database.saveTimeline(roomId, cachedEvents);
        });
    }, Qt::BlockingQueuedConnection);
    if (!saveQueued || !saved)
        return false;

    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0))
        return false;
    MatrixNetwork network;
    MatrixNetworkTestAccess::addUnknownJoinedRoom(network, roomId);
    network.setServerUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
    MatrixNetworkTestAccess::setAccessToken(network, QStringLiteral("synthetic-token"));
    MatrixNetworkTestAccess::setRedactionIdentity(network, QStringLiteral("@self:example.org"));
    if (!QMetaObject::invokeMethod(&network, "setDatabaseWorker", Qt::DirectConnection,
            Q_ARG(QObject *, static_cast<QObject *>(worker))))
        return false;

    int succeeded = -1;
    int total = -1;
    QString error;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&network, &MatrixNetwork::memberRedactionFinished, &loop,
        [&loop, &succeeded, &total, &error](const QString &, const QString &,
            int redacted, int count, const QString &failure) {
            succeeded = redacted;
            total = count;
            error = failure;
            loop.quit();
        });

    QList<QByteArray> requestLines;
    QList<QByteArray> bodies;
    bool validAuthorization = true;
    const auto handleRequest = [&requestLines, &bodies, &validAuthorization](QTcpSocket *socket) {
        QByteArray request = socket->property("redactionRequestBuffer").toByteArray();
        request += socket->readAll();
        socket->setProperty("redactionRequestBuffer", request);
        const int headerEnd = request.indexOf(QByteArrayLiteral("\r\n\r\n"));
        if (headerEnd < 0)
            return;
        int contentLength = 0;
        const QByteArray headers = request.left(headerEnd);
        for (const QByteArray &line : headers.split('\n'))
            if (line.toLower().startsWith("content-length:"))
                contentLength = line.mid(line.indexOf(':') + 1).trimmed().toInt();
        if (request.size() < headerEnd + 4 + contentLength)
            return;
        requestLines.append(headers.left(headers.indexOf('\r')));
        bodies.append(request.mid(headerEnd + 4, contentLength));
        validAuthorization = validAuthorization &&
            headers.contains("Authorization: Bearer synthetic-token");
        const QByteArray responseBody = QByteArrayLiteral("{\"event_id\":\"$redacted\"}");
        QByteArray response = QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ");
        response += QByteArray::number(responseBody.size());
        response += QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + responseBody;
        socket->write(response);
        socket->disconnectFromHost();
    };
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&server, handleRequest]() {
        while (server.hasPendingConnections()) {
            QTcpSocket *socket = server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::readyRead, socket,
                [socket, handleRequest]() { handleRequest(socket); });
            if (socket->bytesAvailable() > 0)
                handleRequest(socket);
        }
    });

    timeout.start(3000);
    network.redactMemberMessages(roomId, memberId, QStringLiteral("test reason"));
    loop.exec();
    MatrixNetworkTestAccess::setAccessToken(network, QString());
    QMetaObject::invokeMethod(&network, "setDatabaseWorker", Qt::DirectConnection,
        Q_ARG(QObject *, static_cast<QObject *>(nullptr)));
    const bool validTargets = requestLines.size() == 2 &&
        requestLines.at(0).startsWith("PUT ") &&
        requestLines.at(0).contains("/redact/%24redact-one%3Aexample.org/") &&
        requestLines.at(1).startsWith("PUT ") &&
        requestLines.at(1).contains("/redact/%24redact-two%3Aexample.org/");
    const bool validBodies = bodies.size() == 2 &&
        QJsonDocument::fromJson(bodies.value(0)).object().value(QStringLiteral("reason")).toString() ==
            QStringLiteral("test reason") &&
        QJsonDocument::fromJson(bodies.value(1)).object().value(QStringLiteral("reason")).toString() ==
            QStringLiteral("test reason");
    return succeeded == 2 && total == 2 && error.isEmpty() && validTargets &&
        validAuthorization && validBodies;
}

bool testExistingDirectChatReuse()
{
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0))
        return false;

    MatrixNetwork network;
    const QString ownUserId = QStringLiteral("@self:example.org");
    const QString contactUserId = QStringLiteral("@contact:example.org");
    MatrixNetworkTestAccess::setUserId(network, ownUserId);
    MatrixNetworkTestAccess::setAccessToken(network, QStringLiteral("synthetic-token"));
    network.setServerUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
    MatrixNetworkTestAccess::addDirectRoom(network, QStringLiteral("!other-contact:example.org"),
        {ownUserId, QStringLiteral("@other:example.org")}, true, true);
    MatrixNetworkTestAccess::addDirectRoom(network, QStringLiteral("!not-direct:example.org"),
        {ownUserId, contactUserId}, false, true);
    MatrixNetworkTestAccess::addDirectRoom(network, QStringLiteral("!not-joined:example.org"),
        {ownUserId, contactUserId}, true, false);
    const QString matchingRoomId = QStringLiteral("!matching-direct:example.org");
    MatrixNetworkTestAccess::addDirectRoom(network, matchingRoomId,
        {ownUserId, contactUserId}, true, true);

    QString createdForUser;
    QString openedRoomId;
    QString error;
    int requestCount = 0;
    QObject::connect(&network, &MatrixNetwork::directRoomCreated, &network,
        [&createdForUser, &openedRoomId, &error](const QString &userId,
            const QString &roomId, const QString &failure) {
            createdForUser = userId;
            openedRoomId = roomId;
            error = failure;
        });
    QObject::connect(&server, &QTcpServer::newConnection, &server,
        [&server, &requestCount]() {
            while (server.hasPendingConnections()) {
                ++requestCount;
                QTcpSocket *socket = server.nextPendingConnection();
                socket->abort();
                socket->deleteLater();
            }
        });

    network.startDirectChat(contactUserId);
    QEventLoop loop;
    QTimer::singleShot(150, &loop, &QEventLoop::quit);
    loop.exec();
    MatrixNetworkTestAccess::setAccessToken(network, QString());
    return createdForUser == contactUserId && openedRoomId == matchingRoomId &&
        error.isEmpty() && requestCount == 0;
}

int fail(const char *message)
{
    std::cerr << message << '\n';
    return 1;
}

bool createLegacyRoomsDatabase(const QString &profileDirectory)
{
    const QString connectionName = QStringLiteral("matrix_room_encryption_legacy");
    bool created = false;
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(profileDirectory + QStringLiteral("/matrix.db"));
        if (database.open()) {
            QSqlQuery query(database);
            created = query.exec(QStringLiteral(
                "CREATE TABLE rooms (room_id TEXT PRIMARY KEY, room_type TEXT, room_version TEXT, "
                "name TEXT, topic TEXT, avatar_url TEXT, membership TEXT, "
                "is_direct INTEGER NOT NULL DEFAULT 0, is_encrypted INTEGER NOT NULL DEFAULT 0, "
                "replacement_room_id TEXT, prev_batch TEXT, limited INTEGER NOT NULL DEFAULT 0)"))
                && query.exec(QStringLiteral(
                    "INSERT INTO rooms (room_id, name, membership, is_encrypted) VALUES "
                    "('!legacy-encrypted:example.org', 'Legacy encrypted', 'join', 1)"))
                && query.exec(QStringLiteral(
                    "INSERT INTO rooms (room_id, name, membership, is_encrypted) VALUES "
                    "('!legacy-clear:example.org', 'Legacy clear', 'join', 0)"));
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return created;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (!testExistingDirectChatReuse())
        return fail("Send Message did not reuse the joined direct room for the exact contact");
    QTemporaryDir profileDirectory;
    if (!profileDirectory.isValid())
        return fail("could not create temporary Matrix profile");
    if (!createLegacyRoomsDatabase(profileDirectory.path()))
        return fail("could not create legacy Matrix database fixture");

    QThread databaseThread;
    auto *worker = new MatrixDatabaseWorker;
    worker->moveToThread(&databaseThread);
    QObject::connect(&databaseThread, &QThread::finished, worker, &QObject::deleteLater);
    databaseThread.start();

    bool opened = false;
    QMetaObject::invokeMethod(worker, [&]() {
        opened = worker->openForAccount(profileDirectory.path(),
            QStringLiteral("https://matrix.example"), QStringLiteral("@test:example.org"));
    }, Qt::BlockingQueuedConnection);
    if (!opened) {
        databaseThread.quit();
        databaseThread.wait();
        return fail("could not open test Matrix database");
    }

    bool legacyEncrypted = false;
    bool legacyEncryptedKnown = false;
    const bool foundLegacyEncrypted = readStoredEncryption(worker,
        QStringLiteral("!legacy-encrypted:example.org"), legacyEncrypted, legacyEncryptedKnown);
    bool legacyClear = true;
    bool legacyClearKnown = true;
    const bool foundLegacyClear = readStoredEncryption(worker,
        QStringLiteral("!legacy-clear:example.org"), legacyClear, legacyClearKnown);

    MatrixNetwork network;
    if (!QMetaObject::invokeMethod(&network, "setDatabaseWorker", Qt::DirectConnection,
            Q_ARG(QObject *, static_cast<QObject *>(worker)))) {
        QMetaObject::invokeMethod(worker, [worker]() { worker->close(); }, Qt::BlockingQueuedConnection);
        databaseThread.quit();
        databaseThread.wait();
        return fail("could not attach test Matrix database worker");
    }
    MatrixNetworkTestAccess::restorePersistedRooms(network);

    const QString roomId = QStringLiteral("!timeline-encryption:example.org");
    const QString clearRoomId = QStringLiteral("!cleartext-room:example.org");
    const QJsonObject roomNameEvent{{QStringLiteral("type"), QStringLiteral("m.room.name")},
        {QStringLiteral("state_key"), QString()},
        {QStringLiteral("content"), QJsonObject{{QStringLiteral("name"), QStringLiteral("Encrypted room")}}}};
    const QJsonObject encryptionEvent{{QStringLiteral("type"), QStringLiteral("m.room.encryption")},
        {QStringLiteral("event_id"), QStringLiteral("$encryption-state")},
        {QStringLiteral("state_key"), QString()},
        {QStringLiteral("sender"), QStringLiteral("@test:example.org")},
        {QStringLiteral("origin_server_ts"), 1700000000000LL},
        {QStringLiteral("content"), QJsonObject{{QStringLiteral("algorithm"),
            QStringLiteral("m.megolm.v1.aes-sha2")}}}};
    const QJsonObject powerLevelsContent{{QStringLiteral("users_default"), 0},
        {QStringLiteral("kick"), 50}, {QStringLiteral("ban"), 50}};
    const QJsonObject powerLevelsEvent{{QStringLiteral("type"), QStringLiteral("m.room.power_levels")},
        {QStringLiteral("state_key"), QString()}, {QStringLiteral("content"), powerLevelsContent}};
    const QJsonObject roomData{{QStringLiteral("state"), QJsonObject{{QStringLiteral("events"),
            QJsonArray{roomNameEvent, powerLevelsEvent}}}},
        {QStringLiteral("timeline"), QJsonObject{{QStringLiteral("prev_batch"), QStringLiteral("$prev")},
            {QStringLiteral("events"), QJsonArray{encryptionEvent}}}}};
    const QJsonObject clearRoomData{{QStringLiteral("state"), QJsonObject{{QStringLiteral("events"),
            QJsonArray{roomNameEvent}}}},
        {QStringLiteral("timeline"), QJsonObject{{QStringLiteral("prev_batch"), QStringLiteral("$prev-clear")},
            {QStringLiteral("events"), QJsonArray{}}}}};
    const QJsonObject response{{QStringLiteral("next_batch"), QStringLiteral("$next")},
        {QStringLiteral("rooms"), QJsonObject{{QStringLiteral("join"),
            QJsonObject{{roomId, roomData}, {clearRoomId, clearRoomData}}}}}};
    QString receivedPowerLevelsRoomId;
    QJsonObject receivedPowerLevels;
    QObject::connect(&network, &MatrixNetwork::roomPowerLevelsChanged,
        [&receivedPowerLevelsRoomId, &receivedPowerLevels](const QString &receivedRoomId,
            const QJsonObject &content) {
            receivedPowerLevelsRoomId = receivedRoomId;
            receivedPowerLevels = content;
        });
    auto *reply = new SyntheticSyncReply(QJsonDocument(response).toJson(QJsonDocument::Compact),
        &network);
    MatrixNetworkTestAccess::processSyncResponse(network, reply);
    MatrixNetworkTestAccess::suppressBackgroundSync(network);
    if (receivedPowerLevelsRoomId != roomId || receivedPowerLevels != powerLevelsContent)
        return fail("sync did not expose the current room power-level state");

    bool persistedEncrypted = false;
    bool persistedEncryptedKnown = false;
    const bool foundRoom = readStoredEncryption(worker, roomId, persistedEncrypted,
        persistedEncryptedKnown);
    bool persistedClearEncrypted = true;
    bool persistedClearKnown = false;
    const bool foundClearRoom = readStoredEncryption(worker, clearRoomId, persistedClearEncrypted,
        persistedClearKnown);
    MatrixNetwork restored;
    const bool restoredWorker = QMetaObject::invokeMethod(&restored, "setDatabaseWorker",
        Qt::DirectConnection, Q_ARG(QObject *, static_cast<QObject *>(worker)));
    if (restoredWorker)
        MatrixNetworkTestAccess::restorePersistedRooms(restored);
    const bool restoredEncrypted = MatrixNetworkTestAccess::roomIsEncrypted(restored, roomId);
    const bool restoredClearKnown = MatrixNetworkTestAccess::roomEncryptionStateKnown(restored, clearRoomId);
    const bool restoredClearEncrypted = MatrixNetworkTestAccess::roomIsEncrypted(restored, clearRoomId);
    const bool restoredLegacyEncryptedKnown = MatrixNetworkTestAccess::roomEncryptionStateKnown(
        restored, QStringLiteral("!legacy-encrypted:example.org"));
    const bool restoredLegacyClearKnown = MatrixNetworkTestAccess::roomEncryptionStateKnown(
        restored, QStringLiteral("!legacy-clear:example.org"));
    const QString unknownRoomId = QStringLiteral("!legacy-clear:example.org");
    const bool unknownRoomPresent = MatrixNetworkTestAccess::roomExists(network, unknownRoomId);
    const bool unknownRoomKnownBeforeSend = MatrixNetworkTestAccess::roomEncryptionStateKnown(network, unknownRoomId);
    const bool unknownSendRetried = testRoomEncryptionLookup(network, unknownRoomId, false);
    bool resolvedLegacyClearEncrypted = true;
    bool resolvedLegacyClearKnown = false;
    const bool foundResolvedLegacyClear = readStoredEncryption(worker, unknownRoomId,
        resolvedLegacyClearEncrypted, resolvedLegacyClearKnown);
    const QString endpointEncryptedRoomId = QStringLiteral("!state-endpoint-encrypted:example.org");
    MatrixNetworkTestAccess::addUnknownJoinedRoom(network, endpointEncryptedRoomId);
    const bool endpointEncryptedStateResolved = testRoomEncryptionLookup(network,
        endpointEncryptedRoomId, true);
    bool endpointEncryptedValue = false;
    bool endpointEncryptedKnown = false;
    const bool foundEndpointEncryptedState = readStoredEncryption(worker, endpointEncryptedRoomId,
        endpointEncryptedValue, endpointEncryptedKnown);
    const bool memberRedactionWorked = testRoomMemberRedaction(worker);
    const bool memberModerationWorked = testRoomMemberModeration();
    if (!unknownSendRetried)
        std::cerr << "unknown room cached=" << unknownRoomPresent << ", status known before send="
            << unknownRoomKnownBeforeSend << '\n';

    QMetaObject::invokeMethod(&network, "setDatabaseWorker", Qt::DirectConnection,
        Q_ARG(QObject *, static_cast<QObject *>(nullptr)));
    QMetaObject::invokeMethod(&restored, "setDatabaseWorker", Qt::DirectConnection,
        Q_ARG(QObject *, static_cast<QObject *>(nullptr)));
    QMetaObject::invokeMethod(worker, [worker]() { worker->close(); }, Qt::BlockingQueuedConnection);
    databaseThread.quit();
    databaseThread.wait();

    if (!MatrixNetworkTestAccess::roomIsEncrypted(network, roomId))
        return fail("sync timeline did not mark the live room encrypted");
    if (!MatrixNetworkTestAccess::roomEncryptionStateKnown(network, roomId))
        return fail("sync timeline did not mark the room encryption state as known");
    if (!foundRoom || !persistedEncrypted)
        return fail("timeline encryption state was not persisted");
    if (!foundLegacyEncrypted || !legacyEncrypted || !legacyEncryptedKnown || !foundLegacyClear ||
        legacyClear || legacyClearKnown || !restoredLegacyEncryptedKnown || restoredLegacyClearKnown)
        return fail("legacy room-state migration did not preserve known/unknown encryption safely");
    if (!persistedEncryptedKnown || !foundClearRoom || !persistedClearKnown || persistedClearEncrypted)
        return fail("known encrypted/unencrypted room state was not persisted");
    if (!restoredWorker || !restoredEncrypted)
        return fail("restored room did not retain timeline encryption state");
    if (!MatrixNetworkTestAccess::roomEncryptionStateKnown(restored, roomId))
        return fail("restored encrypted room did not retain known encryption state");
    if (!MatrixNetworkTestAccess::roomEncryptionStateKnown(network, clearRoomId) ||
        MatrixNetworkTestAccess::roomIsEncrypted(network, clearRoomId))
        return fail("full-state sync did not identify an unencrypted room");
    if (!restoredClearKnown || restoredClearEncrypted)
        return fail("restored unencrypted room did not retain its known clear state");
    if (!unknownSendRetried)
        return fail("message was not retried after the room was confirmed unencrypted");
    if (!unknownRoomPresent || unknownRoomKnownBeforeSend)
        return fail("state lookup test did not start from a cached room with unknown encryption state");
    if (!foundResolvedLegacyClear || !resolvedLegacyClearKnown || resolvedLegacyClearEncrypted)
        return fail("404 room-state lookup result was not persisted as known unencrypted");
    if (!endpointEncryptedStateResolved || !foundEndpointEncryptedState || !endpointEncryptedKnown ||
        !endpointEncryptedValue)
        return fail("200 room-state lookup result was not persisted as known encrypted");
    if (!memberRedactionWorked)
        return fail("cached Matrix messages were not redacted through sequential PUT requests");
    if (!memberModerationWorked)
        return fail("Matrix kick, ban, and power-level updates sent incorrect HTTP requests");
    if (!testRoomPowerLevelsLookup())
        return fail("lazy room power-level lookup did not coalesce or return current state");
    return 0;
}
