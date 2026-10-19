#include "matrix.h"
#include "matrixhistorybackup.h"
#include "matrixdatabaseworker.h"
#include "matrixnetwork.h"
#include "protocolhistoryviewmodel.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QTemporaryDir>

#include <algorithm>
#include <functional>
#include <iostream>

class MatrixDatabaseWorkerFixture
{
public:
    MatrixDatabaseWorkerFixture() : databaseThread(), databaseWorker(nullptr)
    {
        databaseWorker = new MatrixDatabaseWorker();
        databaseWorker->moveToThread(&databaseThread);
        databaseThread.start();
    }

    ~MatrixDatabaseWorkerFixture()
    {
        if (databaseWorker)
            close();
        databaseThread.quit();
        databaseThread.wait();
        delete databaseWorker;
    }

    bool open(const QString &profileDirectory, const QString &serverUrl, const QString &userId)
    {
        bool databaseOpened = false;
        QMetaObject::invokeMethod(databaseWorker, "openForAccount", Qt::BlockingQueuedConnection,
            Q_RETURN_ARG(bool, databaseOpened), Q_ARG(QString, profileDirectory),
            Q_ARG(QString, serverUrl), Q_ARG(QString, userId));
        return databaseOpened;
    }

    void close()
    {
        QMetaObject::invokeMethod(databaseWorker, "close", Qt::BlockingQueuedConnection);
    }

    QThread databaseThread;
    MatrixDatabaseWorker *databaseWorker;
};

class MatrixNetworkTestAccess
{
public:
    static void prepareHistoryBackup(MatrixNetwork &network, const QString &profileDirectory,
        const QString &serverUrl, const QString &userId, const QString &roomId,
        const QList<MatrixTextEvent> &events)
    {
        network.FDatabaseProfileDirectory = profileDirectory;
        network.setHistoryBackupDirectory(profileDirectory);
        network.FNormalizedServerUrl = serverUrl;
        network.FUserId = userId;
        network.FMessageHistory.insert(roomId, events);
    }

    static void saveMessageHistory(MatrixNetwork &network, const QString &roomId)
    {
        network.saveMessageHistory(roomId);
    }

    static void configureInitialSync(MatrixNetwork &network, const QString &serverUrl,
        const QString &userId, const QString &accessToken)
    {
        network.FUserId = userId;
        network.FAccesToken = accessToken;
        network.FSyncFilterId = QStringLiteral("synthetic-filter");
        network.FFilterCreationAttempted = true;
        network.setHistoryBackupDirectory(network.FDatabaseProfileDirectory);
        network.setServerUrl(serverUrl);
    }

    static void clearMessageHistory(MatrixNetwork &network)
    {
        network.FMessageHistory.clear();
    }

    static void configureJoinedRoom(MatrixNetwork &network, const QString &roomId)
    {
        ProtocolRoom room;
        room.id = roomId;
        room.isJoined = true;
        room.encryptionStateKnown = true;
        network.FRooms.insert(roomId, room);
    }

    static void configureSqlHistory(MatrixNetwork &network, const QString &profileDirectory,
        const QString &normalizedServerUrl, const QString &userId)
    {
        network.FDatabaseProfileDirectory = profileDirectory;
        network.FNormalizedServerUrl = normalizedServerUrl;
        network.FUserId = userId;
    }
};

class MatrixSyncBatchTestAccess
{
public:
    static MatrixNetwork *configureBackupRequest(Matrix &matrix,
        const QString &profileDirectory, const QString &archiveDirectory,
        const QString &serverUrl, const QString &userId, QObject *databaseWorker = nullptr)
    {
        matrix.FDatabaseProfileDirectory = profileDirectory;
        matrix.FHistoryBackupDirectory = archiveDirectory;
        matrix.FDatabaseServerUrl = serverUrl;
        matrix.FDatabaseUserId = userId;
        matrix.FMatrixNetwork = new MatrixNetwork;
        matrix.FMatrixNetwork->setDatabaseProfileDirectory(profileDirectory);
        matrix.FMatrixNetwork->setHistoryBackupDirectory(archiveDirectory);
        matrix.FMatrixNetwork->setDatabaseWorker(databaseWorker);
        return matrix.FMatrixNetwork;
    }

    static void clearBackupRequestNetwork(Matrix &matrix)
    {
        matrix.FMatrixNetwork = nullptr;
    }
};

namespace
{
bool check(bool condition, const char *description)
{
    if (!condition)
        std::cerr << "FAIL: " << description << '\n';
    return condition;
}

MatrixTextEvent makeHistoryBackupTextEvent(const QString &userId, const QString &roomId,
    const QString &content)
{
    MatrixTextEvent event;
    event.eventId = QStringLiteral("$event:example.org");
    event.roomId = roomId;
    event.userId = userId;
    event.content = content;
    event.timestamp = QStringLiteral("1700000000000");
    event.eventType = QStringLiteral("m.room.message");
    event.messageType = QStringLiteral("m.text");
    event.metadata.insert(QStringLiteral("sender_is_self"), true);
    return event;
}

bool requestDoesNotExportSqlHistory()
{
    QTemporaryDir profileDirectory;
    QTemporaryDir archiveDirectory;
    if (!check(profileDirectory.isValid() && archiveDirectory.isValid(),
            "SQL-only profile and archive directories are available"))
        return false;

    const QString configuredServerUrl = QStringLiteral("matrix.example.org");
    const QString normalizedServerUrl = QStringLiteral("https://matrix.example.org");
    const QString userId = QStringLiteral("@alice:example.org");
    const QString roomId = QStringLiteral("!sql-room:example.org");
    MatrixTimelineEvent storedEvent;
    storedEvent.roomId = roomId;
    storedEvent.eventId = QStringLiteral("$stored-sql-event:example.org");
    storedEvent.eventType = QStringLiteral("m.room.message");
    storedEvent.sender = userId;
    storedEvent.originTs = 1700000000000LL;
    storedEvent.messageType = QStringLiteral("m.text");
    storedEvent.content = QStringLiteral("history persisted before JSON backup");
    storedEvent.metadata.insert(QStringLiteral("sender_is_self"), true);

    QThread databaseThread;
    auto *databaseWorker = new MatrixDatabaseWorker;
    databaseWorker->moveToThread(&databaseThread);
    databaseThread.start();
    bool databaseOpened = false;
    QMetaObject::invokeMethod(databaseWorker, "openForAccount", Qt::BlockingQueuedConnection,
        Q_RETURN_ARG(bool, databaseOpened), Q_ARG(QString, profileDirectory.path()),
        Q_ARG(QString, normalizedServerUrl), Q_ARG(QString, userId));
    bool eventStored = false;
    if (databaseOpened)
        QMetaObject::invokeMethod(databaseWorker,
            [databaseWorker, &eventStored, &roomId, &storedEvent]() {
                databaseWorker->execute([&](MatrixDatabase &database) {
                    eventStored = database.appendTimelineEvents(roomId, {storedEvent});
                });
            }, Qt::BlockingQueuedConnection);

    MatrixHistoryBackup backup(archiveDirectory.path(), normalizedServerUrl, userId);
    QList<MatrixTextEvent> absentBackup;
    bool passed = check(databaseOpened && eventStored,
        "synthetic SQL timeline event is stored through the existing database") &&
        check(!backup.loadMessageHistory(roomId, absentBackup),
            "the room starts without a JSON backup");

    Matrix matrix;
    MatrixNetwork *network = nullptr;
    if (passed) {
        network = MatrixSyncBatchTestAccess::configureBackupRequest(
            matrix, profileDirectory.path(), archiveDirectory.path(),
            configuredServerUrl, userId, databaseWorker);
        MatrixNetworkTestAccess::configureSqlHistory(*network, profileDirectory.path(),
            normalizedServerUrl, userId);
        QEventLoop loop;
        int callbackCount = 0;
        bool pageValid = false;
        const bool queued = matrix.requestLocalHistoryBackup(roomId, &loop,
            [&loop, &callbackCount, &pageValid](ProtocolHistoryPage page) {
                ++callbackCount;
                pageValid = !page.success && page.messages.isEmpty();
                loop.quit();
            });
        if (queued) {
            QTimer::singleShot(3000, &loop, &QEventLoop::quit);
            loop.exec();
        }
        QList<MatrixTextEvent> exported;
        QList<MatrixTimelineEvent> remainingSqlEvents;
        QMetaObject::invokeMethod(databaseWorker, [databaseWorker, &roomId, &remainingSqlEvents]() {
            databaseWorker->execute([&](MatrixDatabase &database) {
                remainingSqlEvents = database.getEvents(roomId);
            });
        }, Qt::BlockingQueuedConnection);
        passed = check(queued, "Matrix accepts the local-backup history request") &&
            check(callbackCount == 1 && pageValid,
                "SQL-only history is not returned as a JSON-backup page") &&
            check(!backup.loadMessageHistory(roomId, exported) && exported.isEmpty(),
                "SQL-only history does not create or populate an archive JSON file") &&
            check(remainingSqlEvents.size() == 1 &&
                remainingSqlEvents.first().eventId == storedEvent.eventId,
                "the existing Matrix SQL timeline remains unchanged");
    }

    if (network) {
        MatrixSyncBatchTestAccess::clearBackupRequestNetwork(matrix);
        delete network;
    }
    if (databaseOpened)
        QMetaObject::invokeMethod(databaseWorker, "close", Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(databaseWorker, [databaseWorker]() {
        databaseWorker->moveToThread(QCoreApplication::instance()->thread());
    }, Qt::BlockingQueuedConnection);
    databaseThread.quit();
    databaseThread.wait();
    delete databaseWorker;
    return passed;
}

bool localHistoryBackupRequestReturnsBasicMessages()
{
    QTemporaryDir profileDirectory;
    QTemporaryDir archiveDirectory;
    if (!check(profileDirectory.isValid() && archiveDirectory.isValid(),
            "temporary profile and archive directories are available"))
        return false;

    const QString serverUrl = QStringLiteral("https://matrix.example.org");
    const QString userId = QStringLiteral("@alice:example.org");
    const QString roomId = QStringLiteral("!room:example.org");
    const MatrixTextEvent event = makeHistoryBackupTextEvent(userId, roomId,
        QStringLiteral("history from backup"));

    MatrixHistoryBackup backup(archiveDirectory.path(), serverUrl, userId);
    if (!check(backup.saveMessageHistory(roomId, {event}),
            "synthetic Matrix history backup fixture is saved"))
        return false;

    Matrix matrix;
    MatrixNetwork *network = MatrixSyncBatchTestAccess::configureBackupRequest(
        matrix, profileDirectory.path(), archiveDirectory.path(), serverUrl, userId);
    MatrixNetworkTestAccess::configureSqlHistory(*network, profileDirectory.path(), serverUrl, userId);
    QEventLoop loop;
    int callbackCount = 0;
    bool pageValid = false;
    const bool queued = matrix.requestLocalHistoryBackup(roomId, &loop,
        [&loop, &callbackCount, &pageValid, &event, &roomId](ProtocolHistoryPage page) {
            ++callbackCount;
            const QList<ProtocolHistoryDayGroup> groups =
                ProtocolHistoryViewModel::groupByDay(page.messages, roomId, QString());
            pageValid = page.success && page.messages.size() == 1 &&
                page.messages.first().messageId() == event.eventId &&
                page.messages.first().conversationId() == roomId &&
                page.messages.first().body() == event.content &&
                page.messages.first().direction() == BasicMessage::Outgoing &&
                page.messages.first().timestamp().isValid() && groups.size() == 1 &&
                groups.first().messages.size() == 1 &&
                groups.first().messages.first().messageId() == event.eventId;
            loop.quit();
        });
    if (!check(queued, "Matrix accepts the asynchronous local-backup request")) {
        MatrixSyncBatchTestAccess::clearBackupRequestNetwork(matrix);
        delete network;
        return false;
    }

    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    loop.exec();
    MatrixSyncBatchTestAccess::clearBackupRequestNetwork(matrix);
    delete network;

    if (!check(callbackCount == 1 && pageValid,
            "Matrix returns the local backup as protocol-neutral BasicMessage history"))
        return false;

    std::cout << "Matrix local history backup request test passed\n";
    return true;
}

void startJsonHttpServer(QTcpServer &server,
    std::function<QByteArray(const QByteArray &)> bodyForRequest)
{
    QObject::connect(&server, &QTcpServer::newConnection, &server,
        [&server, bodyForRequest]() {
            while (server.hasPendingConnections()) {
                QTcpSocket *socket = server.nextPendingConnection();
                QObject::connect(socket, &QTcpSocket::readyRead, socket,
                    [socket, bodyForRequest]() {
                        QByteArray request = socket->property("requestBuffer").toByteArray();
                        request.append(socket->readAll());
                        socket->setProperty("requestBuffer", request);
                        if (!request.contains("\r\n\r\n") ||
                            socket->property("testReplied").toBool())
                            return;
                        socket->setProperty("testReplied", true);
                        const QByteArray body = bodyForRequest(request);
                        const QByteArray response =
                            QByteArray("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ") +
                            QByteArray::number(body.size()) +
                            "\r\nConnection: close\r\n\r\n" + body;
                        socket->write(response);
                        socket->disconnectFromHost();
                    });
            }
        });
}

bool initialSyncWritesLocalBackup()
{
    QTemporaryDir profileDirectory;
    QTemporaryDir archiveDirectory;
    if (!check(profileDirectory.isValid() && archiveDirectory.isValid(),
            "initial-sync profile and archive directories are available"))
        return false;

    QTcpServer server;
    if (!check(server.listen(QHostAddress::LocalHost, 0), "initial-sync HTTP fixture starts"))
        return false;

    const QString userId = QStringLiteral("@initial-sync:example.org");
    const QString roomId = QStringLiteral("!initial-sync:example.org");
    const QJsonObject initialMessage{
        {QStringLiteral("event_id"), QStringLiteral("$initial-sync:example.org")},
        {QStringLiteral("type"), QStringLiteral("m.room.message")},
        {QStringLiteral("sender"), QStringLiteral("@remote:example.org")},
        {QStringLiteral("origin_server_ts"), 1700000001000LL},
        {QStringLiteral("content"), QJsonObject{
            {QStringLiteral("msgtype"), QStringLiteral("m.text")},
            {QStringLiteral("body"), QStringLiteral("from first sync")}}}};
    const QJsonObject initialRoom{
        {QStringLiteral("state"), QJsonObject{{QStringLiteral("events"), QJsonArray{
            QJsonObject{{QStringLiteral("type"), QStringLiteral("m.room.name")},
                {QStringLiteral("state_key"), QString()},
                {QStringLiteral("content"), QJsonObject{{QStringLiteral("name"), QStringLiteral("Test room")}}}}}}}},
        {QStringLiteral("timeline"), QJsonObject{
            {QStringLiteral("events"), QJsonArray{initialMessage}},
            {QStringLiteral("prev_batch"), QStringLiteral("t_initial")}}}};
    const QByteArray syncBody = QJsonDocument(QJsonObject{
        {QStringLiteral("next_batch"), QStringLiteral("s_initial")},
        {QStringLiteral("rooms"), QJsonObject{{QStringLiteral("join"),
            QJsonObject{{roomId, initialRoom}}}}}}).toJson(QJsonDocument::Compact);
    int syncRequests = 0;
    startJsonHttpServer(server, [&](const QByteArray &request) {
        const bool isSync = request.startsWith("GET /_matrix/client/v3/sync");
        if (isSync)
            ++syncRequests;
        return isSync ? syncBody : QByteArray("{\"chunk\":[]}");
    });

    MatrixDatabaseWorkerFixture dbFixture;
    const QString serverUrl = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
    if (!dbFixture.open(profileDirectory.path(), serverUrl, userId)) {
        return check(false, "initial-sync Matrix SQL cache opens for the synthetic account");
    }

    MatrixNetwork network;
    network.setDatabaseProfileDirectory(profileDirectory.path());
    network.setDatabaseWorker(dbFixture.databaseWorker);
    MatrixNetworkTestAccess::configureInitialSync(network, serverUrl, userId,
        QStringLiteral("synthetic-token"));
    network.setHistoryBackupDirectory(archiveDirectory.path());
    bool initialSyncCompleted = false;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&network, &MatrixNetwork::initialSyncCompleted, &loop, [&]() {
        initialSyncCompleted = true;
        loop.quit();
    });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(5000);
    network.sync();
    if (!initialSyncCompleted)
        loop.exec();

    MatrixHistoryBackup backup(archiveDirectory.path(), serverUrl, userId);
    QList<MatrixTextEvent> savedMessages;
    const bool backupLoaded = backup.loadMessageHistory(roomId, savedMessages);
    const bool passed = check(initialSyncCompleted, "synthetic initial Matrix sync completes") &&
        check(syncRequests == 1, "the local fixture receives exactly one sync request") &&
        check(backupLoaded && savedMessages.size() == 1 &&
            savedMessages.first().eventId == QStringLiteral("$initial-sync:example.org") &&
            savedMessages.first().content == QStringLiteral("from first sync") &&
            savedMessages.first().userId == QStringLiteral("@remote:example.org") &&
            !savedMessages.first().metadata.value(QStringLiteral("sender_is_self")).toBool(),
            "an incoming initial /sync event is written to the separate local history backup");

    network.shutdown();
    return passed;
}

bool outgoingLocalEchoWritesBackupBeforeSync()
{
    QTemporaryDir profileDirectory;
    QTemporaryDir archiveDirectory;
    if (!check(profileDirectory.isValid() && archiveDirectory.isValid(),
            "outgoing-echo profile and archive directories are available"))
        return false;

    QTcpServer server;
    if (!check(server.listen(QHostAddress::LocalHost, 0), "outgoing-echo HTTP fixture starts"))
        return false;

    const QString userId = QStringLiteral("@sender:example.org");
    const QString roomId = QStringLiteral("!outgoing:example.org");
    const QString txnId = QStringLiteral("$local-echo:example.org");
    const QString serverUrl = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());

    MatrixDatabaseWorkerFixture dbFixture;
    if (!dbFixture.open(profileDirectory.path(), serverUrl, userId)) {
        return check(false, "outgoing-echo Matrix SQL cache opens for the synthetic account");
    }

    MatrixNetwork network;
    network.setDatabaseProfileDirectory(profileDirectory.path());
    network.setDatabaseWorker(dbFixture.databaseWorker);
    MatrixNetworkTestAccess::configureInitialSync(network, serverUrl, userId,
        QStringLiteral("synthetic-token"));
    network.setHistoryBackupDirectory(archiveDirectory.path());
    MatrixNetworkTestAccess::configureJoinedRoom(network, roomId);
    network.sendTextMessage(roomId, QStringLiteral("outgoing local echo"), txnId);

    MatrixHistoryBackup backup(archiveDirectory.path(), serverUrl, userId);
    QList<MatrixTextEvent> savedMessages;
    const bool backupLoaded = backup.loadMessageHistory(roomId, savedMessages);
    const bool passed = check(backupLoaded && savedMessages.size() == 1 &&
        savedMessages.first().eventId == txnId &&
        savedMessages.first().content == QStringLiteral("outgoing local echo"),
        "the outgoing local echo reaches the JSON backup before a later /sync echo");

    network.shutdown();
    return passed;
}

bool persistentHistoryBackupIsMergeOnRestart()
{
    // Create a synthetic room in the history backup
    QTemporaryDir profileDirectory;
    QTemporaryDir archiveDirectory;
    if (!check(profileDirectory.isValid() && archiveDirectory.isValid(),
            "test profile and archive directories are available"))
        return false;

    QTcpServer server;
    if (!check(server.listen(QHostAddress::LocalHost, 0), "HTTP fixture starts"))
        return false;

    const QString userId = QStringLiteral("@test:example.org");
    const QString roomId = QStringLiteral("!roomId:example.org");

    // Initial sync events
    const QJsonObject firstMessage{
        {QStringLiteral("event_id"), QStringLiteral("$first-event:example.org")},
        {QStringLiteral("type"), QStringLiteral("m.room.message")},
        {QStringLiteral("sender"), userId},
        {QStringLiteral("origin_server_ts"), 1700000002000LL},
        {QStringLiteral("content"), QJsonObject{
            {QStringLiteral("msgtype"), QStringLiteral("m.text")},
            {QStringLiteral("body"), QStringLiteral("first sync event")}}}};

    // Incremental sync events
    const QJsonObject secondMessage{
        {QStringLiteral("event_id"), QStringLiteral("$second-event:example.org")},
        {QStringLiteral("type"), QStringLiteral("m.room.message")},
        {QStringLiteral("sender"), userId},
        {QStringLiteral("origin_server_ts"), 1700000003000LL},
        {QStringLiteral("content"), QJsonObject{
            {QStringLiteral("msgtype"), QStringLiteral("m.text")},
            {QStringLiteral("body"), QStringLiteral("second sync event")}}}};

    const QJsonObject initialRoom{
        {QStringLiteral("state"), QJsonObject{{QStringLiteral("events"), QJsonArray{
            QJsonObject{{QStringLiteral("type"), QStringLiteral("m.room.name")},
                {QStringLiteral("state_key"), QString()},
                {QStringLiteral("content"), QJsonObject{{QStringLiteral("name"), QStringLiteral("Test room")}}}}}}}},
        {QStringLiteral("timeline"), QJsonObject{
            {QStringLiteral("events"), QJsonArray{firstMessage}},
            {QStringLiteral("prev_batch"), QStringLiteral("t_initial")}}}};

    const QJsonObject incrementalRoom{
        {QStringLiteral("state"), QJsonObject{{QStringLiteral("events"), QJsonArray()}}},
        {QStringLiteral("timeline"), QJsonObject{
            {QStringLiteral("events"), QJsonArray{secondMessage}},
            {QStringLiteral("prev_batch"), QStringLiteral("t_incremental")}}}};

    const QByteArray initialBody = QJsonDocument(QJsonObject{
        {QStringLiteral("next_batch"), QStringLiteral("s_initial")},
        {QStringLiteral("rooms"), QJsonObject{{QStringLiteral("join"),
            QJsonObject{{roomId, initialRoom}}}}}}).toJson(QJsonDocument::Compact);

    const QByteArray incrementalBody = QJsonDocument(QJsonObject{
        {QStringLiteral("next_batch"), QStringLiteral("s_incremental")},
        {QStringLiteral("rooms"), QJsonObject{{QStringLiteral("join"),
            QJsonObject{{roomId, incrementalRoom}}}}}}).toJson(QJsonDocument::Compact);

    int syncRequests = 0;
    startJsonHttpServer(server, [&](const QByteArray &request) {
        const bool isSync = request.startsWith("GET /_matrix/client/v3/sync");
        if (isSync)
            ++syncRequests;
        return isSync ? (syncRequests == 1 ? initialBody : incrementalBody)
                      : QByteArray("{\"chunk\":[]}");
    });

    MatrixDatabaseWorkerFixture dbFixture;
    const QString serverUrl = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
    if (!dbFixture.open(profileDirectory.path(), serverUrl, userId)) {
        return check(false, "Matrix SQL cache opens for the synthetic account");
    }

    MatrixNetwork network;
    network.setDatabaseProfileDirectory(profileDirectory.path());
    network.setDatabaseWorker(dbFixture.databaseWorker);
    MatrixNetworkTestAccess::configureInitialSync(network, serverUrl, userId,
        QStringLiteral("synthetic-token"));
    network.setHistoryBackupDirectory(archiveDirectory.path());

    // Simulate initial sync
    bool initialSyncCompleted = false;
    QEventLoop loop1;
    QTimer timeout1;
    timeout1.setSingleShot(true);
    QObject::connect(&network, &MatrixNetwork::initialSyncCompleted, &loop1, [&]() {
        initialSyncCompleted = true;
        loop1.quit();
    });
    QObject::connect(&timeout1, &QTimer::timeout, &loop1, &QEventLoop::quit);
    timeout1.start(5000);
    network.sync();
    if (!initialSyncCompleted)
        loop1.exec();

    MatrixNetwork restartedNetwork;
    restartedNetwork.setDatabaseProfileDirectory(profileDirectory.path());
    MatrixNetworkTestAccess::configureInitialSync(restartedNetwork, serverUrl, userId,
        QStringLiteral("synthetic-token"));
    restartedNetwork.setHistoryBackupDirectory(archiveDirectory.path());
    QList<MatrixTextEvent> restoredAfterLogin;
    const bool restoredOnRestart =
        restartedNetwork.loadOrCreateLocalHistoryBackup(roomId, restoredAfterLogin);
    restartedNetwork.shutdown();

    // Now we clear the in-memory room history to simulate restart while retaining sync token
    MatrixNetworkTestAccess::clearMessageHistory(network);

    // Then simulate incremental sync to fill in memory again
    bool incrementalSyncCompleted = false;
    QEventLoop loop2;
    QTimer timeout2;
    timeout2.setSingleShot(true);
    QObject::connect(&network, &MatrixNetwork::syncReceived, &loop2, [&]() {
        incrementalSyncCompleted = true;
        loop2.quit();
    });
    QObject::connect(&timeout2, &QTimer::timeout, &loop2, &QEventLoop::quit);
    timeout2.start(5000);

    // Call sync again to get incremental
    network.sync();
    if (!incrementalSyncCompleted)
        loop2.exec();

    MatrixHistoryBackup backup(archiveDirectory.path(), serverUrl, userId);
    QList<MatrixTextEvent> savedMessages;
    const bool backupLoaded = backup.loadMessageHistory(roomId, savedMessages);
    const bool passed =
        check(initialSyncCompleted && incrementalSyncCompleted,
            "initial and incremental syncs complete") &&
        check(restoredOnRestart && restoredAfterLogin.size() == 1 &&
            restoredAfterLogin.first().eventId == QStringLiteral("$first-event:example.org"),
            "a fresh MatrixNetwork reloads the JSON history after login") &&
        check(syncRequests == 2, "the local fixture receives exactly two sync requests") &&
        check(backupLoaded && savedMessages.size() == 2,
            "both initial and incremental events are in the backup") &&
        check(savedMessages.size() == 2 &&
            savedMessages[0].eventId == QStringLiteral("$first-event:example.org") &&
            savedMessages[1].eventId == QStringLiteral("$second-event:example.org"),
            "the events are saved in chronological order");

    network.shutdown();
    return passed;
}

bool liveSyncFixtureWritesJsonBackup()
{
    const QString fixturePath = qEnvironmentVariable("MATRIX_SYNC_FIXTURE_PATH");
    const QString userId = qEnvironmentVariable("MATRIX_USER_ID");
    QFile fixture(fixturePath);
    if (!check(!fixturePath.isEmpty() && !userId.isEmpty() && fixture.open(QIODevice::ReadOnly),
            "live Matrix sync fixture and account identity are available"))
        return false;
    const QByteArray syncBody = fixture.readAll();
    const QJsonDocument syncDocument = QJsonDocument::fromJson(syncBody);
    if (!check(syncDocument.isObject(), "live sync fixture is valid JSON"))
        return false;

    QHash<QString, QSet<QString>> expectedEventIdsByRoom;
    const QJsonObject joinedRooms = syncDocument.object().value(QStringLiteral("rooms"))
        .toObject().value(QStringLiteral("join")).toObject();
    for (auto roomIt = joinedRooms.constBegin(); roomIt != joinedRooms.constEnd(); ++roomIt) {
        const QJsonArray events = roomIt.value().toObject().value(QStringLiteral("timeline"))
            .toObject().value(QStringLiteral("events")).toArray();
        for (const QJsonValue &value : events) {
            const QJsonObject event = value.toObject();
            if (event.value(QStringLiteral("type")).toString() == QStringLiteral("m.room.message")) {
                const QString eventId = event.value(QStringLiteral("event_id")).toString();
                if (!eventId.isEmpty())
                    expectedEventIdsByRoom[roomIt.key()].insert(eventId);
            }
        }
    }
    qsizetype expectedMessageCount = 0;
    for (auto roomIt = expectedEventIdsByRoom.constBegin();
         roomIt != expectedEventIdsByRoom.constEnd(); ++roomIt)
        expectedMessageCount += roomIt.value().size();
    if (!check(expectedMessageCount > 0, "live sync fixture contains message timeline events"))
        return false;

    QTemporaryDir profileDirectory;
    QTcpServer server;
    if (!check(profileDirectory.isValid() && server.listen(QHostAddress::LocalHost, 0),
            "temporary profile and local HTTP fixture are ready"))
        return false;
    startJsonHttpServer(server, [syncBody](const QByteArray &) { return syncBody; });

    const QString serverUrl = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
    MatrixDatabaseWorkerFixture dbFixture;
    if (!dbFixture.open(profileDirectory.path(), serverUrl, userId)) {
        return check(false, "live-fixture Matrix SQL cache opens");
    }

    MatrixNetwork network;
    network.setDatabaseProfileDirectory(profileDirectory.path());
    network.setDatabaseWorker(dbFixture.databaseWorker);
    MatrixNetworkTestAccess::configureInitialSync(network, serverUrl, userId,
        QStringLiteral("local-fixture-token"));
    bool initialSyncCompleted = false;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&network, &MatrixNetwork::initialSyncCompleted, &loop, [&]() {
        initialSyncCompleted = true;
        loop.quit();
    });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(15000);
    network.sync();
    if (!initialSyncCompleted)
        loop.exec();

    MatrixHistoryBackup backup(profileDirectory.path(), serverUrl, userId);
    qsizetype backedUpMessageCount = 0;
    for (auto roomIt = expectedEventIdsByRoom.constBegin();
         roomIt != expectedEventIdsByRoom.constEnd(); ++roomIt) {
        QList<MatrixTextEvent> savedEvents;
        if (!backup.loadMessageHistory(roomIt.key(), savedEvents))
            continue;
        QSet<QString> savedEventIds;
        for (const MatrixTextEvent &event : savedEvents)
            savedEventIds.insert(event.eventId);
        const QSet<QString> &expectedEventIds = roomIt.value();
        backedUpMessageCount += static_cast<qsizetype>(std::count_if(
            expectedEventIds.cbegin(), expectedEventIds.cend(),
            [&](const QString &eventId) { return savedEventIds.contains(eventId); }));
    }

    network.shutdown();
    std::cout << "Live sync-fixture messages: " << expectedMessageCount
        << "; persisted to JSON: " << backedUpMessageCount << '\n';
    return check(initialSyncCompleted && backedUpMessageCount == expectedMessageCount,
        "production /sync processing writes every live-fixture message to a JSON backup");
}

bool sqlOnlyEventDoesNotCreateJsonFile()
{
    QTemporaryDir profileDirectory;
    if (!check(profileDirectory.isValid(), "SQL-only event test profile directory is available"))
        return false;

    const QString userId = QStringLiteral("@sql-only:example.org");
    const QString roomId = QStringLiteral("!sql-only:example.org");
    const QString serverUrl = QStringLiteral("https://matrix.example.org");

    MatrixDatabaseWorkerFixture dbFixture;
    if (!dbFixture.open(profileDirectory.path(), serverUrl, userId)) {
        return check(false, "Matrix SQL cache opens for the synthetic account");
    }

    MatrixTimelineEvent storedEvent;
    storedEvent.roomId = roomId;
    storedEvent.eventId = QStringLiteral("$sql-only-event:example.org");
    storedEvent.sender = userId;
    storedEvent.content = QStringLiteral("from SQL only, not in FMessageHistory");
    storedEvent.originTs = 1700000004000LL;
    storedEvent.eventType = QStringLiteral("m.room.message");
    storedEvent.messageType = QStringLiteral("m.text");
    QList<MatrixTimelineEvent> storedEvents{storedEvent};
    bool eventStored = false;
    const bool seedQueued = QMetaObject::invokeMethod(dbFixture.databaseWorker, [&]() {
        dbFixture.databaseWorker->execute([&](MatrixDatabase &database) {
            eventStored = database.appendTimelineEvents(roomId, storedEvents);
        });
    }, Qt::BlockingQueuedConnection);
    if (!check(seedQueued && eventStored, "SQL-only timeline event is persisted"))
        return false;

    MatrixNetwork network;
    network.setDatabaseProfileDirectory(profileDirectory.path());
    network.setDatabaseWorker(dbFixture.databaseWorker);
    MatrixNetworkTestAccess::configureInitialSync(network, serverUrl, userId,
        QStringLiteral("synthetic-token"));

    QList<MatrixTextEvent> history;
    const bool historyAvailable = network.loadOrCreateLocalHistoryBackup(roomId, history);
    MatrixHistoryBackup backup(profileDirectory.path(), serverUrl, userId);
    QList<MatrixTextEvent> savedMessages;
    const bool jsonBackupExists = backup.loadMessageHistory(roomId, savedMessages);
    const bool passed =
        check(!historyAvailable && history.isEmpty(),
            "SQL-only event is not returned by the local JSON history request") &&
        check(!jsonBackupExists && savedMessages.isEmpty(),
            "SQL-only event does not create or appear in a JSON backup");

    network.shutdown();
    return passed;
}

bool writesToArchiveDirectoryInsteadOfProfile()
{
    QTemporaryDir profileDirectory;
    QTemporaryDir archiveDirectory;
    if (!check(profileDirectory.isValid() && archiveDirectory.isValid(),
            "profile and archive fixture directories are available"))
        return false;

    const QString serverUrl = QStringLiteral("https://matrix.example.org");
    const QString userId = QStringLiteral("@archive-path:example.org");
    const QString roomId = QStringLiteral("!archive-path:example.org");
    MatrixTextEvent event;
    event.eventId = QStringLiteral("$archive-path:example.org");
    event.roomId = roomId;
    event.userId = userId;
    event.content = QStringLiteral("must be under archive root");
    event.timestamp = QStringLiteral("1700000005000");
    event.eventType = QStringLiteral("m.room.message");
    event.messageType = QStringLiteral("m.text");

    MatrixNetwork network;
    MatrixNetworkTestAccess::prepareHistoryBackup(network, profileDirectory.path(),
        serverUrl, userId, roomId, {event});
    const bool configured = QMetaObject::invokeMethod(&network, "setHistoryBackupDirectory",
        Qt::DirectConnection, Q_ARG(QString, archiveDirectory.path()));
    if (!check(configured, "MatrixNetwork accepts a dedicated archive backup directory"))
        return false;

    MatrixNetworkTestAccess::saveMessageHistory(network, roomId);
    MatrixHistoryBackup archiveBackup(archiveDirectory.path(), serverUrl, userId);
    MatrixHistoryBackup profileBackup(profileDirectory.path(), serverUrl, userId);
    QList<MatrixTextEvent> savedMessages;
    QList<MatrixTextEvent> profileMessages;
    const bool passed =
        check(archiveBackup.loadMessageHistory(roomId, savedMessages) &&
            savedMessages.size() == 1 && savedMessages.first().eventId == event.eventId,
            "automatic backup writes the event under the archive directory") &&
        check(!profileBackup.loadMessageHistory(roomId, profileMessages),
            "the Matrix SQL profile directory is not reused as the JSON archive");
    return passed;
}

}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (qEnvironmentVariableIsSet("MATRIX_SYNC_FIXTURE_PATH"))
        return liveSyncFixtureWritesJsonBackup() ? 0 : 1;
    if (!requestDoesNotExportSqlHistory())
        return 1;
    if (!localHistoryBackupRequestReturnsBasicMessages())
        return 1;
    QTemporaryDir profileDirectory;
    if (!check(profileDirectory.isValid(), "temporary profile directory is available"))
        return 1;

    const QString serverUrl = QStringLiteral("https://matrix.example.org");
    const QString userId = QStringLiteral("@alice:example.org");
    const QString roomId = QStringLiteral("!room:example.org");
    const MatrixTextEvent event = makeHistoryBackupTextEvent(userId, roomId,
        QStringLiteral("backup message"));

    MatrixNetwork network;
    MatrixNetworkTestAccess::prepareHistoryBackup(network, profileDirectory.path(),
        serverUrl, userId, roomId, {event});
    MatrixNetworkTestAccess::saveMessageHistory(network, roomId);

    MatrixHistoryBackup backup(profileDirectory.path(), serverUrl, userId);
    QList<MatrixTextEvent> restored;
    if (!check(backup.loadMessageHistory(roomId, restored),
            "MatrixNetwork persists its cached room history to the independent local backup") ||
        !check(restored.size() == 1 && restored.first().eventId == event.eventId &&
            restored.first().content == event.content &&
            restored.first().metadata.value(QStringLiteral("sender_is_self")).toBool(),
            "the local backup preserves message identity, body, and direction metadata"))
        return 1;

    if (!initialSyncWritesLocalBackup())
        return 1;

    if (!outgoingLocalEchoWritesBackupBeforeSync())
        return 1;

    if (!persistentHistoryBackupIsMergeOnRestart())
        return 1;

    if (!sqlOnlyEventDoesNotCreateJsonFile())
        return 1;

    if (!writesToArchiveDirectoryInsteadOfProfile())
        return 1;

    std::cout << "Matrix network history backup test passed\n";
    return 0;
}
