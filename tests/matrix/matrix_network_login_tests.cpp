#include "matrixdatabaseworker.h"
#include "matrixnetwork.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QSemaphore>
#include <QMetaObject>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <atomic>
#include <functional>
#include <iostream>
#include <thread>

class MatrixNetworkTestAccess
{
public:
    static bool isLoggedIn(const MatrixNetwork &network)
    {
        return !network.FAccesToken.isEmpty();
    }

    static bool initialSyncComplete(const MatrixNetwork &network)
    {
        return network.FInitialSyncComplete;
    }

    static bool syncInFlight(const MatrixNetwork &network)
    {
        return network.FSyncInFlight;
    }

    static QList<ProtocolRoom> rooms(const MatrixNetwork &network)
    {
        QList<ProtocolRoom> result;
        for (const ProtocolRoom &room : network.FRooms)
            result.append(room);
        return result;
    }

    static void setAccessToken(MatrixNetwork &network, const QString &accessToken)
    {
        network.FAccesToken = accessToken;
    }

    static void setRoom(MatrixNetwork &network, const QString &roomId)
    {
        ProtocolRoom room;
        room.id = roomId;
        room.membership = QStringLiteral("join");
        room.isJoined = true;
        room.isAvailable = true;
        network.FRooms.insert(roomId, room);
    }

    static void configureRoomsOnlySync(MatrixNetwork &network)
    {
        network.FAccesToken = QStringLiteral("synthetic-rooms-refresh-token");
        network.FSyncToken = QStringLiteral("test-next-batch");
        network.FSyncFilterId = QStringLiteral("stored-filter-id");
    }

    static bool hasStoredEvent(MatrixDatabaseWorker *worker, const QString &roomId,
                               const QString &eventId)
    {
        bool found = false;
        QMetaObject::invokeMethod(worker, [worker, roomId, eventId, &found]() {
            const QList<MatrixTimelineEvent> stored = worker->events(roomId);
            found = std::any_of(stored.cbegin(), stored.cend(), [&eventId](const MatrixTimelineEvent &event) {
                return event.eventId == eventId;
            });
        }, Qt::BlockingQueuedConnection);
        return found;
    }

    static bool hasStoredMembers(MatrixDatabaseWorker *worker, const QString &roomId,
                                 const QStringList &memberIds)
    {
        bool found = false;
        QMetaObject::invokeMethod(worker, [worker, roomId, memberIds, &found]() {
            QList<MatrixStoredMember> stored;
            worker->execute([&](MatrixDatabase &database) {
                stored = database.roomMembers(roomId);
            });
            QSet<QString> storedIds;
            for (const MatrixStoredMember &member : stored)
                storedIds.insert(member.userId);
            found = std::all_of(memberIds.cbegin(), memberIds.cend(),
                [&storedIds](const QString &id) { return storedIds.contains(id); });
        }, Qt::BlockingQueuedConnection);
        return found;
    }
};

class MatrixLocalApi final : public QTcpServer
{
public:
    std::function<void(const QString &)> requestReceived;
    bool holdSyncResponses = false;
    bool holdRoomMembershipResponses = false;

    struct CapturedRequest
    {
        QString method;
        QString path;
        QByteArray body;
        QByteArray authorization;
    };

    MatrixLocalApi()
    {
        QObject::connect(this, &QTcpServer::newConnection, this, [this]() {
            while (hasPendingConnections()) {
                QTcpSocket *socket = nextPendingConnection();
                QObject::connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
                    handleRequest(socket);
                });
                QObject::connect(socket, &QTcpSocket::disconnected,
                    socket, &QObject::deleteLater);
            }
        });
    }

    QList<CapturedRequest> requests() const { return FRequests; }

    QStringList requestPaths() const
    {
        QStringList paths;
        for (const CapturedRequest &request : FRequests)
            paths.append(request.path);
        return paths;
    }

    void setHoldSyncResponses(bool hold) { holdSyncResponses = hold; }
    void setHoldRoomMembershipResponses(bool hold) { holdRoomMembershipResponses = hold; }

private:
    bool readRequest(QTcpSocket *socket, CapturedRequest &captured)
    {
        QByteArray request = socket->property("request").toByteArray();
        request += socket->readAll();
        socket->setProperty("request", request);
        const int headerEnd = request.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return false;

        const QList<QByteArray> lines = request.left(headerEnd).split('\n');
        const QList<QByteArray> requestLine = lines.value(0).trimmed().split(' ');
        if (requestLine.size() < 2) {
            socket->abort();
            return false;
        }
        int contentLength = 0;
        QByteArray authorization;
        for (int i = 1; i < lines.size(); ++i) {
            const QByteArray line = lines.at(i).trimmed();
            const int colon = line.indexOf(':');
            if (colon <= 0)
                continue;
            const QByteArray name = line.left(colon).trimmed();
            const QByteArray value = line.mid(colon + 1).trimmed();
            if (name.compare(QByteArrayLiteral("Content-Length"), Qt::CaseInsensitive) == 0)
                contentLength = value.toInt();
            else if (name.compare(QByteArrayLiteral("Authorization"), Qt::CaseInsensitive) == 0)
                authorization = value;
        }
        if (request.size() < headerEnd + 4 + contentLength)
            return false;

        captured.method = QString::fromLatin1(requestLine.at(0));
        captured.path = QString::fromLatin1(requestLine.at(1));
        captured.body = request.mid(headerEnd + 4, contentLength);
        captured.authorization = authorization;
        return true;
    }

    void handleRequest(QTcpSocket *socket)
    {
        CapturedRequest request;
        if (!readRequest(socket, request))
            return;
        FRequests.append(request);
        if (requestReceived)
            requestReceived(request.path);
        if (holdSyncResponses && request.path.startsWith(QStringLiteral("/_matrix/client/v3/sync")))
            return;
        if (holdRoomMembershipResponses &&
            (request.path.contains(QStringLiteral("/joined_members")) ||
             request.path.startsWith(QStringLiteral("/_matrix/client/v3/rooms/"))))
            return;
        const QByteArray body = responseBodyForPath(request.path);
        QByteArray response = QByteArrayLiteral(
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ");
        response += QByteArray::number(body.size());
        response += QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body;
        socket->write(response);
        socket->disconnectFromHost();
    }

    QByteArray responseBodyForPath(const QString &path)
    {
        if (path == QStringLiteral("/_matrix/client/v3/login"))
            return QByteArrayLiteral(
                "{\"user_id\":\"@matrix-login-test:example.invalid\","
                "\"access_token\":\"synthetic-password-access-token\","
                "\"device_id\":\"SYNTHETICDEVICE\"}");
        if (path.startsWith(QStringLiteral("/_matrix/client/v3/sync"))) {
            const int syncResponseNumber = ++FSyncResponseCount;
            QJsonArray timelineEvents;
            for (int index = 0; index < 128; ++index) {
                timelineEvents.append(QJsonObject{
                    {QStringLiteral("type"), QStringLiteral("m.room.message")},
                    {QStringLiteral("event_id"), QStringLiteral("$ordinary-%1-%2")
                        .arg(syncResponseNumber).arg(index)},
                    {QStringLiteral("sender"), QStringLiteral("@alice:example.invalid")},
                    {QStringLiteral("origin_server_ts"), 1700000000000LL + index},
                    {QStringLiteral("content"), QJsonObject{
                        {QStringLiteral("msgtype"), QStringLiteral("m.text")},
                        {QStringLiteral("body"), QStringLiteral("ordinary test message %1").arg(index)}}}});
            }
            timelineEvents.append(QJsonObject{
                {QStringLiteral("type"), QStringLiteral("m.reaction")},
                {QStringLiteral("event_id"), QStringLiteral("$reaction-notification")},
                {QStringLiteral("sender"), QStringLiteral("@alice:example.invalid")},
                {QStringLiteral("origin_server_ts"), 1700000001000LL},
                {QStringLiteral("content"), QJsonObject{
                    {QStringLiteral("m.relates_to"), QJsonObject{
                        {QStringLiteral("rel_type"), QStringLiteral("m.annotation")},
                        {QStringLiteral("event_id"), QStringLiteral("$target-event")},
                        {QStringLiteral("key"), QStringLiteral("👍")}}}}}});
            const QJsonObject joinedRoom{
                {QStringLiteral("state"), QJsonObject{{QStringLiteral("events"), QJsonArray{}}}},
                {QStringLiteral("timeline"), QJsonObject{{QStringLiteral("events"), timelineEvents}}}};
            const QJsonArray inviteEvents{
                QJsonObject{{QStringLiteral("type"), QStringLiteral("m.room.member")},
                    {QStringLiteral("sender"), QStringLiteral("@alice:example.invalid")},
                    {QStringLiteral("state_key"), QStringLiteral("@matrix-login-test:example.invalid")},
                    {QStringLiteral("content"), QJsonObject{{QStringLiteral("membership"), QStringLiteral("invite")}}}},
                QJsonObject{{QStringLiteral("type"), QStringLiteral("m.room.name")},
                    {QStringLiteral("state_key"), QString()},
                    {QStringLiteral("content"), QJsonObject{{QStringLiteral("name"), QStringLiteral("Matrix invitation test")}}}}};
            const QJsonObject invitedRoom{
                {QStringLiteral("invite_state"), QJsonObject{{QStringLiteral("events"), inviteEvents}}}};
            return QJsonDocument(QJsonObject{
                {QStringLiteral("next_batch"), QStringLiteral("test-next-batch")},
                {QStringLiteral("rooms"), QJsonObject{
                    {QStringLiteral("join"), QJsonObject{
                        {QStringLiteral("!login-test:example.invalid"), joinedRoom},
                        {QStringLiteral("!unopened-room:example.invalid"), joinedRoom}}},
                    {QStringLiteral("invite"), QJsonObject{{QStringLiteral("!invite-test:example.invalid"), invitedRoom}}},
                    {QStringLiteral("leave"), QJsonObject{}}}},
                {QStringLiteral("to_device"), QJsonObject{{QStringLiteral("events"), QJsonArray{}}}},
                {QStringLiteral("presence"), QJsonObject{{QStringLiteral("events"), QJsonArray{}}}},
                {QStringLiteral("device_lists"), QJsonObject{
                    {QStringLiteral("changed"), QJsonArray{}},
                    {QStringLiteral("left"), QJsonArray{}}}}}).toJson(QJsonDocument::Compact);
        }
        if (path.contains(QStringLiteral("/joined_members")))
            return QJsonDocument(QJsonObject{{QStringLiteral("joined"), QJsonObject{
                {QStringLiteral("@alice:example.invalid"), QJsonObject{
                    {QStringLiteral("display_name"), QStringLiteral("Alice")}}},
                {QStringLiteral("@bob:example.invalid"), QJsonObject{
                    {QStringLiteral("display_name"), QStringLiteral("Bob")}}}}}})
                .toJson(QJsonDocument::Compact);
        if (path.startsWith(QStringLiteral("/_matrix/client/v3/keys/query")))
            return QByteArrayLiteral("{\"device_keys\":{},\"failures\":{}}");
        if (path.startsWith(QStringLiteral("/_matrix/client/v3/join/")) ||
            path.endsWith(QStringLiteral("/join")))
            return QByteArrayLiteral("{\"room_id\":\"!unknown-room:example.invalid\"}");
        if (path.contains(QStringLiteral("/send/")))
            return QByteArrayLiteral("{\"event_id\":\"$matrix-local-send-event\"}");
        return QByteArrayLiteral("{}");
    }

    QList<CapturedRequest> FRequests;
    int FSyncResponseCount = 0;
};

namespace
{
bool check(bool condition, const char *message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

bool waitUntil(const std::function<bool()> &condition, int timeoutMs)
{
    if (condition())
        return true;
    QEventLoop loop;
    QTimer poll;
    poll.setInterval(5);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&]() {
        if (condition())
            loop.quit();
    });
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start();
    timeout.start(timeoutMs);
    loop.exec();
    poll.stop();
    return condition();
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir profileDirectory;
    if (!check(profileDirectory.isValid(), "temporary Matrix profile directory is available"))
        return 1;

    MatrixLocalApi server;
    if (!check(server.listen(QHostAddress::LocalHost, 0), "local Matrix test server is available"))
        return 1;

    QThread databaseThread;
    auto *databaseWorker = new MatrixDatabaseWorker;
    databaseWorker->moveToThread(&databaseThread);
    QObject::connect(&databaseThread, &QThread::finished,
        databaseWorker, &QObject::deleteLater);
    databaseThread.start();

    bool passed = true;
    {
        MatrixNetwork network;
        passed &= check(MatrixNetwork::staticMetaObject.indexOfSignal(
            "roomInviteReceived(QString,QString,QString)") >= 0,
            "Matrix network exposes incoming room invitations");
        network.setServerUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
        network.setDatabaseProfileDirectory(profileDirectory.path());
        network.setDatabaseWorker(databaseWorker);

        int successCount = 0;
        int errorCount = 0;
        int perEventMessageSignalCount = 0;
        int syncBatchSignalCount = 0;
        int ordinaryMessageNotificationEvents = 0;
        int reactionNotificationEvents = 0;
        QString lastLoginError;
        QString loggedInUser;
        QString loggedInDevice;
        quint64 successGeneration = 0;
        int inviteCount = 0;
        QString invitedRoomId;
        QString inviterId;
        QString invitedRoomName;
        QObject::connect(&network, &MatrixNetwork::roomInviteReceived, &application,
            [&](const QString &roomId, const QString &inviter, const QString &roomName) {
                ++inviteCount;
                invitedRoomId = roomId;
                inviterId = inviter;
                invitedRoomName = roomName;
            });
        QObject::connect(&network, &MatrixNetwork::notificationEventReceived, &application,
            [&](const MatrixNotificationEvent &event) {
                if (event.type == QStringLiteral("m.room.message"))
                    ++ordinaryMessageNotificationEvents;
                else if (event.type == QStringLiteral("m.reaction"))
                    ++reactionNotificationEvents;
            });
        QObject::connect(&network, &MatrixNetwork::messageReceived, &application,
            [&](const BasicMessage &) { ++perEventMessageSignalCount; });
        QObject::connect(&network, &MatrixNetwork::syncReceived, &application,
            [&](const QList<MatrixTextEvent> &events) {
                if (!events.isEmpty())
                    ++syncBatchSignalCount;
            });
        QObject::connect(&network, &MatrixNetwork::loginSuccessForSession, &application,
            [&](const QString &userId, const QString &, const QString &deviceId, quint64 generation) {
                ++successCount;
                loggedInUser = userId;
                loggedInDevice = deviceId;
                successGeneration = generation;
            });
        QObject::connect(&network, &MatrixNetwork::loginErrorForSession, &application,
            [&](const QString &reason, quint64) {
                ++errorCount;
                lastLoginError = reason;
            });

        network.loginWithAccessTokenForSession(
            QStringLiteral("@matrix-login-test:example.invalid"),
            QStringLiteral("synthetic-access-token"),
            QStringLiteral("SYNTHETICDEVICE"), 17);

        passed &= check(successCount == 1,
            "access-token login succeeds when optional libolm is unavailable");
        passed &= check(errorCount == 0,
            "missing optional libolm does not emit a session login error");
        passed &= check(loggedInUser == QStringLiteral("@matrix-login-test:example.invalid") &&
            loggedInDevice == QStringLiteral("SYNTHETICDEVICE") && successGeneration == 17,
            "login success preserves the requested Matrix session identity");
        passed &= check(MatrixNetworkTestAccess::isLoggedIn(network),
            "successful access-token login retains the access-token session");
        if (successCount == 0 && !lastLoginError.isEmpty())
            std::cerr << "Observed login error: " << lastLoginError.toStdString() << '\n';

        const bool syncProcessed = waitUntil([&]() {
            return MatrixNetworkTestAccess::initialSyncComplete(network) &&
                !MatrixNetworkTestAccess::syncInFlight(network);
        }, 3000);
        passed &= check(syncProcessed, "successful login processes the initial Matrix sync response");
        bool syncRequested = false;
        for (const QString &path : server.requestPaths())
            syncRequested |= path.startsWith(QStringLiteral("/_matrix/client/v3/sync"));
        passed &= check(syncRequested,
            "successful login starts the normal Matrix sync request");
        const auto joinedMembersRequestCount = [&server]() {
            const QStringList paths = server.requestPaths();
            return std::count_if(paths.cbegin(), paths.cend(), [](const QString &path) {
                return path.contains(QStringLiteral("/joined_members"));
            });
        };
        passed &= check(joinedMembersRequestCount() == 0,
            "initial sync does not fetch full member lists for unopened rooms");
        passed &= check(perEventMessageSignalCount == 0 && syncBatchSignalCount == 0,
            "initial sync persists timeline events without UI per-message or batch signals");
        passed &= check(MatrixNetworkTestAccess::hasStoredEvent(databaseWorker,
                QStringLiteral("!login-test:example.invalid"), QStringLiteral("$ordinary-1-127")) &&
            MatrixNetworkTestAccess::hasStoredEvent(databaseWorker,
                QStringLiteral("!unopened-room:example.invalid"), QStringLiteral("$ordinary-1-127")),
            "initial sync stores timeline events for opened and unopened rooms");

        const bool inviteDelivered = waitUntil([&]() { return inviteCount > 0; }, 1500);
        passed &= check(inviteDelivered && inviteCount == 1,
            "incoming /sync room invitation emits one user-facing invite signal");
        passed &= check(ordinaryMessageNotificationEvents == 0,
            "ordinary timeline messages do not enqueue unused notification callbacks");
        passed &= check(reactionNotificationEvents > 0,
            "reaction events retain their user-facing notification callback");
        passed &= check(invitedRoomId == QStringLiteral("!invite-test:example.invalid") &&
            inviterId == QStringLiteral("@alice:example.invalid") &&
            invitedRoomName == QStringLiteral("Matrix invitation test"),
            "invite signal identifies the room, inviter, and display name");
        const QList<MatrixLocalApi::CapturedRequest> requestsBeforeDuplicate = server.requests();
        const int syncCountBeforeDuplicate = std::count_if(requestsBeforeDuplicate.cbegin(),
            requestsBeforeDuplicate.cend(), [](const MatrixLocalApi::CapturedRequest &request) {
                return request.path.startsWith(QStringLiteral("/_matrix/client/v3/sync"));
            });
        network.sync();
        const bool incrementalSyncProcessed = waitUntil([&]() {
            const QList<MatrixLocalApi::CapturedRequest> requests = server.requests();
            return std::count_if(requests.cbegin(), requests.cend(),
                [](const MatrixLocalApi::CapturedRequest &request) {
                    return request.path.startsWith(QStringLiteral("/_matrix/client/v3/sync"));
                }) > syncCountBeforeDuplicate && !MatrixNetworkTestAccess::syncInFlight(network);
        }, 1500);
        passed &= check(incrementalSyncProcessed && inviteCount == 1,
            "incremental sync processes repeated room invitation only once");
        passed &= check(perEventMessageSignalCount == 0 && syncBatchSignalCount > 0,
            "incremental sync delivers aggregated UI batches without per-message signals");
        MatrixLocalApi roomsOnlySyncServer;
        passed &= check(roomsOnlySyncServer.listen(QHostAddress::LocalHost, 0),
            "rooms-only sync test server is available");
        roomsOnlySyncServer.setHoldSyncResponses(true);
        roomsOnlySyncServer.setHoldRoomMembershipResponses(true);
        MatrixNetwork roomsOnlySyncProbe;
        roomsOnlySyncProbe.setServerUrl(QStringLiteral("http://127.0.0.1:%1")
            .arg(roomsOnlySyncServer.serverPort()));
        MatrixNetworkTestAccess::configureRoomsOnlySync(roomsOnlySyncProbe);
        roomsOnlySyncProbe.sync(true);
        const bool roomsOnlySyncRequestCaptured = waitUntil([&]() {
            return std::any_of(roomsOnlySyncServer.requests().cbegin(),
                roomsOnlySyncServer.requests().cend(), [](const MatrixLocalApi::CapturedRequest &request) {
                    if (!request.path.startsWith(QStringLiteral("/_matrix/client/v3/sync")))
                        return false;
                    const QUrlQuery query(QUrl(QStringLiteral("https://example.invalid") + request.path));
                    const QJsonObject filter = QJsonDocument::fromJson(
                        query.queryItemValue(QStringLiteral("filter")).toUtf8()).object();
                    return query.queryItemValue(QStringLiteral("since")) == QStringLiteral("test-next-batch") &&
                        filter.value(QStringLiteral("room")).toObject()
                            .value(QStringLiteral("timeline")).toObject()
                            .value(QStringLiteral("limit")).toInt(-1) == 0 &&
                        query.queryItemValue(QStringLiteral("full_state")) != QStringLiteral("true");
                });
        }, 1500);
        passed &= check(roomsOnlySyncRequestCaptured,
            "room-list refresh uses inline zero-timeline incremental state, not full_state");
        roomsOnlySyncProbe.shutdown();

        const QString activeRoomId = QStringLiteral("!login-test:example.invalid");

        QTemporaryDir memberProfileDirectory;
        QThread memberDatabaseThread;
        auto *memberDatabaseWorker = new MatrixDatabaseWorker;
        memberDatabaseWorker->moveToThread(&memberDatabaseThread);
        QObject::connect(&memberDatabaseThread, &QThread::finished,
            memberDatabaseWorker, &QObject::deleteLater);
        memberDatabaseThread.start();
        bool memberDatabaseOpened = false;
        QMetaObject::invokeMethod(memberDatabaseWorker, "openForAccount",
            Qt::BlockingQueuedConnection, Q_RETURN_ARG(bool, memberDatabaseOpened),
            Q_ARG(QString, memberProfileDirectory.path()),
            Q_ARG(QString, QStringLiteral("https://example.invalid")),
            Q_ARG(QString, QStringLiteral("@member-test:example.invalid")));
        passed &= check(memberProfileDirectory.isValid() && memberDatabaseOpened,
            "isolated joined-members database opens");

        MatrixLocalApi memberServer;
        passed &= check(memberServer.listen(QHostAddress::LocalHost, 0),
            "isolated joined-members server is available");
        MatrixNetwork memberProbe;
        memberProbe.setServerUrl(QStringLiteral("http://127.0.0.1:%1")
            .arg(memberServer.serverPort()));
        memberProbe.setDatabaseWorker(memberDatabaseWorker);
        memberServer.setHoldRoomMembershipResponses(true);
        MatrixNetworkTestAccess::setAccessToken(memberProbe,
            QStringLiteral("synthetic-members-token"));
        MatrixNetworkTestAccess::setRoom(memberProbe, activeRoomId);
        memberProbe.setActiveRoom(activeRoomId);

        QSemaphore databaseWorkerStarted;
        QSemaphore releaseDatabaseWorker;
        std::atomic<bool> databaseWorkerReleased{false};
        QMetaObject::invokeMethod(memberDatabaseWorker, [&]() {
            databaseWorkerStarted.release();
            releaseDatabaseWorker.acquire();
            databaseWorkerReleased.store(true);
        }, Qt::QueuedConnection);
        const bool databaseWorkerBlocked = databaseWorkerStarted.tryAcquire(1, 1000);
        passed &= check(databaseWorkerBlocked,
            "isolated database worker can be held for joined-members responsiveness test");
        int joinedMembersRosterUpdates = 0;
        bool rosterUpdatedBeforeDatabaseRelease = false;
        QObject::connect(&memberProbe, &MatrixNetwork::roomMetadataChanged, &application,
            [&](const ProtocolRoom &room) {
                if (room.id == activeRoomId && room.members.size() == 2) {
                    ++joinedMembersRosterUpdates;
                    rosterUpdatedBeforeDatabaseRelease = !databaseWorkerReleased.load();
                }
            });
        bool joinedMembersResponseServed = false;
        memberServer.requestReceived = [&joinedMembersResponseServed](const QString &path) {
            if (path.contains(QStringLiteral("/joined_members")))
                joinedMembersResponseServed = true;
        };
        std::thread databaseWorkerReleaser([&]() {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            releaseDatabaseWorker.release();
        });
        memberProbe.requestJoinedMembers(activeRoomId);
        const bool membersCompletedWhileWorkerBlocked = waitUntil([&]() {
            return joinedMembersResponseServed && joinedMembersRosterUpdates > 0 &&
                !databaseWorkerReleased.load();
        }, 1200);
        databaseWorkerReleaser.join();
        passed &= check(membersCompletedWhileWorkerBlocked && rosterUpdatedBeforeDatabaseRelease,
            "joined-members roster update completes before its blocked database worker is released");
        const QStringList memberRequestPaths = memberServer.requestPaths();
        const bool memberProbeStartedSync = std::any_of(memberRequestPaths.cbegin(),
            memberRequestPaths.cend(), [](const QString &path) {
                return path.startsWith(QStringLiteral("/_matrix/client/v3/sync"));
            });
        passed &= check(!memberProbeStartedSync,
            "isolated joined-members request does not start a sync");
        passed &= check(MatrixNetworkTestAccess::hasStoredMembers(memberDatabaseWorker,
                activeRoomId,
                {QStringLiteral("@alice:example.invalid"), QStringLiteral("@bob:example.invalid")}),
            "joined members persist after asynchronous database work completes");
        memberProbe.shutdown();
        QMetaObject::invokeMethod(memberDatabaseWorker, [memberDatabaseWorker]() {
            memberDatabaseWorker->close();
        }, Qt::BlockingQueuedConnection);
        memberDatabaseThread.quit();
        memberDatabaseThread.wait();

        const QString roomId = QStringLiteral("!login-test:example.invalid");
        const QString transactionId = QStringLiteral("m_local_send_01");
        const QString expectedSendPath = QStringLiteral("/_matrix/client/v3/rooms/") +
            QString::fromUtf8(QUrl::toPercentEncoding(roomId)) +
            QStringLiteral("/send/m.room.message/") + transactionId;
        QString deliveryStatus;
        QString serverEventId;
        QObject::connect(&network, &MatrixNetwork::messageDeliveryChanged, &application,
            [&](const QString &conversationId, const QString &sentTransactionId,
                const QString &status, const QString &eventId) {
                if (conversationId == roomId && sentTransactionId == transactionId) {
                    deliveryStatus = status;
                    serverEventId = eventId;
                }
            });
        const QJsonObject content{{QStringLiteral("msgtype"), QStringLiteral("m.text")},
            {QStringLiteral("body"), QStringLiteral("local Matrix send regression")}};
        network.sendRoomEvent(roomId, QStringLiteral("m.room.message"), content, transactionId);

        const bool sendFinished = waitUntil([&]() {
            return deliveryStatus == QStringLiteral("sent") ||
                deliveryStatus == QStringLiteral("failed");
        }, 3000);
        passed &= check(sendFinished && deliveryStatus == QStringLiteral("sent"),
            "unencrypted Matrix message completes successfully through the send endpoint");
        passed &= check(serverEventId == QStringLiteral("$matrix-local-send-event"),
            "message delivery reports the event id returned by the server");

        MatrixLocalApi::CapturedRequest capturedSendRequest;
        const QList<MatrixLocalApi::CapturedRequest> capturedRequests = server.requests();
        const auto sendRequest = std::find_if(capturedRequests.cbegin(), capturedRequests.cend(),
            [&](const MatrixLocalApi::CapturedRequest &request) {
                return request.method == QStringLiteral("PUT") && request.path == expectedSendPath;
            });
        const bool sendRequestFound = sendRequest != capturedRequests.cend();
        if (sendRequestFound)
            capturedSendRequest = *sendRequest;
        passed &= check(sendRequestFound,
            "Matrix emits PUT /rooms/{roomId}/send/m.room.message/{txnId}");
        passed &= check(capturedSendRequest.authorization ==
            QByteArrayLiteral("Bearer synthetic-access-token"),
            "outgoing Matrix request carries the logged-in access token");
        const QJsonObject sentContent = QJsonDocument::fromJson(capturedSendRequest.body).object();
        passed &= check(sentContent.value(QStringLiteral("msgtype")).toString() == QStringLiteral("m.text") &&
            sentContent.value(QStringLiteral("body")).toString() ==
                QStringLiteral("local Matrix send regression"),
            "outgoing Matrix request preserves the message event content");

        MatrixLocalApi membershipServer;
        passed &= check(membershipServer.listen(QHostAddress::LocalHost, 0),
            "isolated room-membership test server is available");
        MatrixNetwork membershipProbe;
        membershipProbe.setServerUrl(QStringLiteral("http://127.0.0.1:%1")
            .arg(membershipServer.serverPort()));
        MatrixNetworkTestAccess::setAccessToken(membershipProbe,
            QStringLiteral("synthetic-membership-token"));
        const QString roomAlias = QStringLiteral("#os2world:matrix.org");
        const QString expectedJoinPath = QStringLiteral("/_matrix/client/v3/join/") +
            QString::fromUtf8(QUrl::toPercentEncoding(roomAlias));
        membershipProbe.joinRoom(roomAlias);
        const bool joinedUnknownRoomAndSynced = waitUntil([&]() {
            const QList<MatrixLocalApi::CapturedRequest> requests = membershipServer.requests();
            int joinRequestIndex = -1;
            bool syncAfterJoin = false;
            for (int i = 0; i < requests.size(); ++i) {
                const MatrixLocalApi::CapturedRequest &request = requests.at(i);
                if (joinRequestIndex < 0 && request.method == QStringLiteral("POST") &&
                    request.path == expectedJoinPath)
                    joinRequestIndex = i;
                else if (joinRequestIndex >= 0 &&
                    request.path.startsWith(QStringLiteral("/_matrix/client/v3/sync")))
                    syncAfterJoin = true;
            }
            return joinRequestIndex >= 0 && syncAfterJoin;
        }, 1500);
        passed &= check(joinedUnknownRoomAndSynced,
            "joining a Matrix room alias uses the alias join endpoint and syncs after success");
        membershipProbe.shutdown();

        network.logout();
        bool logoutRequestFound = false;
        MatrixLocalApi::CapturedRequest capturedLogoutRequest;
        auto findLogoutRequest = [&]() {
            const QList<MatrixLocalApi::CapturedRequest> capturedRequests = server.requests();
            const auto logoutRequest = std::find_if(capturedRequests.cbegin(), capturedRequests.cend(),
                [](const MatrixLocalApi::CapturedRequest &request) {
                    return request.path == QStringLiteral("/_matrix/client/v3/logout");
                });
            if (logoutRequest == capturedRequests.cend())
                return;
            capturedLogoutRequest = *logoutRequest;
            logoutRequestFound = true;
        };
        const bool logoutRequestCaptured = waitUntil([&]() {
            findLogoutRequest();
            return logoutRequestFound;
        }, 500);
        passed &= check(logoutRequestCaptured && capturedLogoutRequest.method == QStringLiteral("POST"),
            "Matrix logout revokes the server session with POST /logout");
        passed &= check(capturedLogoutRequest.authorization ==
            QByteArrayLiteral("Bearer synthetic-access-token"),
            "Matrix logout request authenticates with the active access token");
        passed &= check(!MatrixNetworkTestAccess::isLoggedIn(network),
            "Matrix logout clears local session state immediately");

        const int requestsBeforePasswordLogin = server.requests().size();
        network.loginForSession(QStringLiteral("@matrix-login-test:example.invalid"),
            QStringLiteral("synthetic-password"), QStringLiteral("SYNTHETICDEVICE"), 29);
        const bool passwordLoginCompleted = waitUntil([&]() {
            return successCount == 2 && MatrixNetworkTestAccess::initialSyncComplete(network);
        }, 3000);
        passed &= check(passwordLoginCompleted && successCount == 2 && errorCount == 0 &&
            successGeneration == 29 &&
            MatrixNetworkTestAccess::isLoggedIn(network),
            "password login restores the Matrix session");
        bool resumedPersistedCursor = false;
        for (int i = requestsBeforePasswordLogin; i < server.requests().size(); ++i) {
            const QString path = server.requests().at(i).path;
            resumedPersistedCursor = resumedPersistedCursor ||
                (path.startsWith(QStringLiteral("/_matrix/client/v3/sync")) &&
                    path.contains(QStringLiteral("since=test-next-batch")));
        }
        passed &= check(resumedPersistedCursor,
            "password login resumes sync from the persisted Matrix cursor");

        network.shutdown();
        network.setDatabaseWorker(nullptr);
    }

    QMetaObject::invokeMethod(databaseWorker, [databaseWorker]() {
        databaseWorker->close();
    }, Qt::BlockingQueuedConnection);
    databaseThread.quit();
    databaseThread.wait();
    server.close();
    return passed ? 0 : 1;
}
