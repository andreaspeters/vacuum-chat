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
    const QJsonObject roomData{{QStringLiteral("state"), QJsonObject{{QStringLiteral("events"),
            QJsonArray{roomNameEvent}}}},
        {QStringLiteral("timeline"), QJsonObject{{QStringLiteral("prev_batch"), QStringLiteral("$prev")},
            {QStringLiteral("events"), QJsonArray{encryptionEvent}}}}};
    const QJsonObject clearRoomData{{QStringLiteral("state"), QJsonObject{{QStringLiteral("events"),
            QJsonArray{roomNameEvent}}}},
        {QStringLiteral("timeline"), QJsonObject{{QStringLiteral("prev_batch"), QStringLiteral("$prev-clear")},
            {QStringLiteral("events"), QJsonArray{}}}}};
    const QJsonObject response{{QStringLiteral("next_batch"), QStringLiteral("$next")},
        {QStringLiteral("rooms"), QJsonObject{{QStringLiteral("join"),
            QJsonObject{{roomId, roomData}, {clearRoomId, clearRoomData}}}}}};
    auto *reply = new SyntheticSyncReply(QJsonDocument(response).toJson(QJsonDocument::Compact),
        &network);
    MatrixNetworkTestAccess::processSyncResponse(network, reply);
    MatrixNetworkTestAccess::suppressBackgroundSync(network);

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
    return 0;
}
