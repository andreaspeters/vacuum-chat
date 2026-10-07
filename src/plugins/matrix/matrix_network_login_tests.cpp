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

#include <iostream>

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
                    QByteArray request = socket->property("request").toByteArray();
                    request += socket->readAll();
                    socket->setProperty("request", request);
                    const int headerEnd = request.indexOf("\r\n\r\n");
                    if (headerEnd < 0)
                        return;

                    const QList<QByteArray> lines = request.left(headerEnd).split('\n');
                    const QList<QByteArray> requestLine = lines.value(0).trimmed().split(' ');
                    if (requestLine.size() < 2) {
                        socket->abort();
                        return;
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
                        return;

                    const QString path = QString::fromLatin1(requestLine.at(1));
                    const QByteArray requestBody = request.mid(headerEnd + 4, contentLength);
                    FRequests.append({QString::fromLatin1(requestLine.at(0)), path,
                        requestBody, authorization});
                    QByteArray body = QByteArrayLiteral("{}");
                    if (path.startsWith(QStringLiteral("/_matrix/client/v3/sync"))) {
                        body = QByteArrayLiteral(
                            "{\"next_batch\":\"test-next-batch\",\"rooms\":{\"join\":{},\"invite\":{},\"leave\":{}},\"to_device\":{\"events\":[]},\"presence\":{\"events\":[]},\"device_lists\":{\"changed\":[],\"left\":[]}}");
                    } else if (path.startsWith(QStringLiteral("/_matrix/client/v3/keys/query"))) {
                        body = QByteArrayLiteral("{\"device_keys\":{},\"failures\":{}}");
                    } else if (path.contains(QStringLiteral("/send/"))) {
                        body = QByteArrayLiteral("{\"event_id\":\"$matrix-local-send-event\"}");
                    }
                    QByteArray response = QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ") +
                        QByteArray::number(body.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body;
                    socket->write(response);
                    socket->disconnectFromHost();
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
        network.setServerUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
        network.setDatabaseProfileDirectory(profileDirectory.path());
        network.setDatabaseWorker(databaseWorker);

        int successCount = 0;
        int errorCount = 0;
        QString lastLoginError;
        QString loggedInUser;
        QString loggedInDevice;
        quint64 successGeneration = 0;
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
        passed &= check(network.isLoggedIn(),
            "successful access-token login retains the access-token session");
        if (successCount == 0 && !lastLoginError.isEmpty())
            std::cerr << "Observed login error: " << lastLoginError.toStdString() << '\n';

        QEventLoop loop;
        QTimer poll;
        poll.setInterval(5);
        QObject::connect(&poll, &QTimer::timeout, &loop, [&]() {
            for (const QString &path : server.requestPaths()) {
                if (path.startsWith(QStringLiteral("/_matrix/client/v3/sync"))) {
                    loop.quit();
                    return;
                }
            }
        });
        QTimer timeout;
        timeout.setSingleShot(true);
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        poll.start();
        timeout.start(3000);
        bool syncRequested = false;
        for (const QString &path : server.requestPaths())
            syncRequested |= path.startsWith(QStringLiteral("/_matrix/client/v3/sync"));
        if (!syncRequested)
            loop.exec();
        poll.stop();
        for (const QString &path : server.requestPaths())
            syncRequested |= path.startsWith(QStringLiteral("/_matrix/client/v3/sync"));
        passed &= check(syncRequested,
            "successful login starts the normal Matrix sync request");

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

        QEventLoop sendLoop;
        QTimer sendTimeout;
        sendTimeout.setSingleShot(true);
        QObject::connect(&sendTimeout, &QTimer::timeout, &sendLoop, &QEventLoop::quit);
        QTimer sendPoll;
        sendPoll.setInterval(5);
        QObject::connect(&sendPoll, &QTimer::timeout, &sendLoop, [&]() {
            if (deliveryStatus == QStringLiteral("sent") ||
                deliveryStatus == QStringLiteral("failed"))
                sendLoop.quit();
        });
        if (deliveryStatus != QStringLiteral("sent") && deliveryStatus != QStringLiteral("failed")) {
            sendPoll.start();
            sendTimeout.start(3000);
            sendLoop.exec();
            sendPoll.stop();
        }
        passed &= check(deliveryStatus == QStringLiteral("sent"),
            "unencrypted Matrix message completes successfully through the send endpoint");
        passed &= check(serverEventId == QStringLiteral("$matrix-local-send-event"),
            "message delivery reports the event id returned by the server");

        MatrixLocalApi::CapturedRequest capturedSendRequest;
        bool sendRequestFound = false;
        for (const MatrixLocalApi::CapturedRequest &request : server.requests()) {
            if (request.method == QStringLiteral("PUT") && request.path == expectedSendPath) {
                capturedSendRequest = request;
                sendRequestFound = true;
                break;
            }
        }
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

        network.logout();
        bool logoutRequestFound = false;
        MatrixLocalApi::CapturedRequest capturedLogoutRequest;
        auto findLogoutRequest = [&]() {
            for (const MatrixLocalApi::CapturedRequest &request : server.requests()) {
                if (request.path == QStringLiteral("/_matrix/client/v3/logout")) {
                    capturedLogoutRequest = request;
                    logoutRequestFound = true;
                    return;
                }
            }
        };
        findLogoutRequest();
        if (!logoutRequestFound) {
            QEventLoop logoutLoop;
            QTimer logoutPoll;
            logoutPoll.setInterval(5);
            QObject::connect(&logoutPoll, &QTimer::timeout, &logoutLoop, [&]() {
                findLogoutRequest();
                if (logoutRequestFound)
                    logoutLoop.quit();
            });
            QTimer logoutTimeout;
            logoutTimeout.setSingleShot(true);
            QObject::connect(&logoutTimeout, &QTimer::timeout, &logoutLoop, &QEventLoop::quit);
            logoutPoll.start();
            logoutTimeout.start(500);
            logoutLoop.exec();
            logoutPoll.stop();
        }
        passed &= check(logoutRequestFound && capturedLogoutRequest.method == QStringLiteral("POST"),
            "Matrix logout revokes the server session with POST /logout");
        passed &= check(capturedLogoutRequest.authorization ==
            QByteArrayLiteral("Bearer synthetic-access-token"),
            "Matrix logout request authenticates with the active access token");
        passed &= check(!network.isLoggedIn(),
            "Matrix logout clears local session state immediately");

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
