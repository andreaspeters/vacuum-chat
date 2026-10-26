#include "matrixdatabaseworker.h"
#include "matrixnetwork.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QMetaObject>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <functional>
#include <iostream>

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
};

class MatrixLocalApi final : public QTcpServer
{
public:
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
        const QByteArray body = responseBodyForPath(request.path);
        QByteArray response = QByteArrayLiteral(
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ");
        response += QByteArray::number(body.size());
        response += QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body;
        socket->write(response);
        socket->disconnectFromHost();
    }

    static QByteArray responseBodyForPath(const QString &path)
    {
        if (path == QStringLiteral("/_matrix/client/v3/login"))
            return QByteArrayLiteral(
                "{\"user_id\":\"@matrix-login-test:example.invalid\","
                "\"access_token\":\"synthetic-password-access-token\","
                "\"device_id\":\"SYNTHETICDEVICE\"}");
        if (path.startsWith(QStringLiteral("/_matrix/client/v3/sync")))
            return QByteArrayLiteral(
                "{\"next_batch\":\"test-next-batch\",\"rooms\":{"
                "\"join\":{\"!login-test:example.invalid\":{\"state\":{\"events\":[]},"
                "\"timeline\":{\"events\":[]}}},"
                "\"invite\":{\"!invite-test:example.invalid\":{\"invite_state\":{\"events\":["
                "{\"type\":\"m.room.member\",\"sender\":\"@alice:example.invalid\","
                "\"state_key\":\"@matrix-login-test:example.invalid\","
                "\"content\":{\"membership\":\"invite\"}},"
                "{\"type\":\"m.room.name\",\"state_key\":\"\","
                "\"content\":{\"name\":\"Matrix invitation test\"}}]}}},\"leave\":{}},"
                "\"to_device\":{\"events\":[]},\"presence\":{\"events\":[]},"
                "\"device_lists\":{\"changed\":[],\"left\":[]}}");
        if (path.startsWith(QStringLiteral("/_matrix/client/v3/keys/query")))
            return QByteArrayLiteral("{\"device_keys\":{},\"failures\":{}}");
        if (path.endsWith(QStringLiteral("/join")))
            return QByteArrayLiteral("{\"room_id\":\"!unknown-room:example.invalid\"}");
        if (path.contains(QStringLiteral("/send/")))
            return QByteArrayLiteral("{\"event_id\":\"$matrix-local-send-event\"}");
        return QByteArrayLiteral("{}");
    }

    QList<CapturedRequest> FRequests;
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
            return MatrixNetworkTestAccess::initialSyncComplete(network);
        }, 3000);
        passed &= check(syncProcessed, "successful login processes the initial Matrix sync response");
        bool syncRequested = false;
        for (const QString &path : server.requestPaths())
            syncRequested |= path.startsWith(QStringLiteral("/_matrix/client/v3/sync"));
        passed &= check(syncRequested,
            "successful login starts the normal Matrix sync request");

        const bool inviteDelivered = waitUntil([&]() { return inviteCount > 0; }, 1500);
        passed &= check(inviteDelivered && inviteCount == 1,
            "incoming /sync room invitation emits one user-facing invite signal");
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
        const bool duplicateSyncProcessed = waitUntil([&]() {
            const QList<MatrixLocalApi::CapturedRequest> requests = server.requests();
            return std::count_if(requests.cbegin(), requests.cend(),
                [](const MatrixLocalApi::CapturedRequest &request) {
                    return request.path.startsWith(QStringLiteral("/_matrix/client/v3/sync"));
                }) > syncCountBeforeDuplicate;
        }, 1500);
        passed &= check(duplicateSyncProcessed && inviteCount == 1,
            "repeated sync delivery of the same invite does not prompt again");

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

        const QString unknownRoomId = QStringLiteral("!unknown-room:example.invalid");
        const QString expectedJoinPath = QStringLiteral("/_matrix/client/v3/rooms/") +
            QString::fromUtf8(QUrl::toPercentEncoding(unknownRoomId)) + QStringLiteral("/join");
        const int syncCountBeforeJoin = std::count_if(capturedRequests.cbegin(),
            capturedRequests.cend(), [](const MatrixLocalApi::CapturedRequest &request) {
                return request.path.startsWith(QStringLiteral("/_matrix/client/v3/sync"));
            });
        network.joinRoom(unknownRoomId);
        const bool joinedUnknownRoomAndSynced = waitUntil([&]() {
            const QList<MatrixLocalApi::CapturedRequest> requests = server.requests();
            const bool joinRequestFound = std::any_of(requests.cbegin(), requests.cend(),
                [&](const MatrixLocalApi::CapturedRequest &request) {
                    return request.method == QStringLiteral("POST") && request.path == expectedJoinPath;
                });
            const int syncCount = std::count_if(requests.cbegin(), requests.cend(),
                [](const MatrixLocalApi::CapturedRequest &request) {
                    return request.path.startsWith(QStringLiteral("/_matrix/client/v3/sync"));
                });
            return joinRequestFound && syncCount > syncCountBeforeJoin;
        }, 1500);
        passed &= check(joinedUnknownRoomAndSynced,
            "joining an unknown public room requests a fresh sync after the join succeeds");

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
