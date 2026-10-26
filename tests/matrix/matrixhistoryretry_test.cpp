#include "matrixdatabaseworker.h"
#include "matrixnetwork.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QMetaObject>
#include <QHostAddress>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <iostream>
#include <memory>

class MatrixNetworkTestAccess
{
public:
    static bool mergeMessageEvent(MatrixNetwork &network, const MatrixTextEvent &event,
                                  bool fromHistoryBackfill = false)
    {
        return network.mergeMessageEvent(event, fromHistoryBackfill);
    }

    static void setAccessToken(MatrixNetwork &network, const QString &token)
    {
        network.FAccesToken = token;
    }

    static void setUserId(MatrixNetwork &network, const QString &userId)
    {
        network.FUserId = userId;
    }

    static void prepareBackfill(MatrixNetwork &network, const QString &roomId,
                                const QString &cursor)
    {
        ProtocolRoom room;
        room.id = roomId;
        network.FRooms.insert(roomId, room);
        network.FRoomPrevBatch.insert(roomId, cursor);
    }

    static void prepareSync(MatrixNetwork &network)
    {
        network.FFilterCreationAttempted = true;
    }

    static QList<MatrixTextEvent> history(const MatrixNetwork &network, const QString &roomId)
    {
        return network.FMessageHistory.value(roomId);
    }
};

class HistorySignalCounter : public QObject
{
    Q_OBJECT
public:
    int count = 0;

public slots:
    void onHistoryChanged(const QString &, const QList<MatrixTextEvent> &)
    {
        ++count;
    }
};

class MessageReceivedCounter : public QObject
{
    Q_OBJECT
public:
    int count = 0;

public slots:
    void onMessageReceived(const BasicMessage &)
    {
        ++count;
    }
};

namespace {
int fail(const char *message)
{
    std::cerr << message << '\n';
    return 1;
}

template<typename Predicate>
bool waitForEventLoop(QEventLoop &loop, int timeoutMs, Predicate isComplete)
{
    if (isComplete())
        return true;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(timeoutMs);
    loop.exec();
    return isComplete();
}

QString mediaCachePath(const QString &profileDirectory, const QString &mxcUrl)
{
    return profileDirectory + QStringLiteral("/media/") +
        QString::fromLatin1(QCryptographicHash::hash(mxcUrl.toUtf8(),
            QCryptographicHash::Sha256).toHex()) + QStringLiteral(".bin");
}

bool seedPng(const QString &path)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QImage source(8, 8, QImage::Format_ARGB32);
    source.fill(Qt::blue);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !source.save(&buffer, "PNG"))
        return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray syntheticPng()
{
    QImage image(8, 8, QImage::Format_ARGB32);
    image.fill(Qt::blue);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
        return QByteArray();
    return bytes;
}

class SyntheticMediaServer : public QTcpServer
{
public:
    explicit SyntheticMediaServer(const QByteArray &png, const QByteArray &errorCode,
                                  QObject *parent = nullptr)
        : QTcpServer(parent), FImageBytes(png), FErrorCode(errorCode)
    {
        connect(this, &QTcpServer::newConnection, this, [this]() {
            acceptConnections();
        });
    }

    QStringList requestPaths() const { return FRequestPaths; }
    QStringList authorizationHeaders() const
    {
        QStringList headers;
        for (const QByteArray &header : FAuthorizationHeaders)
            headers.append(QString::fromLatin1(header));
        return headers;
    }

private:
    void acceptConnections()
    {
        while (hasPendingConnections()) {
            QTcpSocket *socket = nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
                processRequest(socket);
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            if (socket->bytesAvailable() > 0)
                processRequest(socket);
        }
    }

    void processRequest(QTcpSocket *socket)
    {
        if (socket->property("matrix-test-replied").toBool())
            return;
        QByteArray request = socket->property("matrix-test-request").toByteArray();
        request += socket->readAll();
        socket->setProperty("matrix-test-request", request);
        const int headerEnd = request.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;

        const QList<QByteArray> lines = request.left(headerEnd).split('\n');
        const QList<QByteArray> requestParts = lines.value(0).trimmed().split(' ');
        if (requestParts.size() < 2) {
            socket->abort();
            return;
        }
        const QString path = QString::fromLatin1(requestParts.at(1));
        QByteArray authorization;
        for (int i = 1; i < lines.size(); ++i) {
            const QByteArray line = lines.at(i).trimmed();
            const int colon = line.indexOf(':');
            if (colon > 0 && line.left(colon).trimmed().compare(
                    "Authorization", Qt::CaseInsensitive) == 0) {
                authorization = line.mid(colon + 1).trimmed();
                break;
            }
        }
        FRequestPaths.append(path);
        FAuthorizationHeaders.append(authorization);
        socket->setProperty("matrix-test-replied", true);

        const bool legacy = path.startsWith(QStringLiteral("/_matrix/media/v3/download/"));
        const QByteArray body = legacy ? FImageBytes : QByteArray("{\"errcode\":\"") +
            FErrorCode + QByteArray("\",\"error\":\"Unknown endpoint\"}");
        const QByteArray status = legacy ? "200 OK" : "404 Not Found";
        const QByteArray contentType = legacy ? "image/png" : "application/json";
        QByteArray response = "HTTP/1.1 " + status + "\r\n";
        response += "Content-Type: " + contentType + "\r\n";
        response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
        response += "Connection: close\r\n\r\n";
        response += body;
        socket->write(response);
        socket->disconnectFromHost();
    }

    QByteArray FImageBytes;
    QByteArray FErrorCode;
    QStringList FRequestPaths;
    QList<QByteArray> FAuthorizationHeaders;
};

class HistoryBackfillServer : public QTcpServer
{
public:
    explicit HistoryBackfillServer(QObject *parent = nullptr) : QTcpServer(parent)
    {
        connect(this, &QTcpServer::newConnection, this, [this]() {
            acceptConnections();
        });
    }

    QStringList targets() const { return FTargets; }

private:
    void acceptConnections()
    {
        while (hasPendingConnections()) {
            QTcpSocket *socket = nextPendingConnection();
            auto request = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::readyRead, this, [this, socket, request]() {
                processRequest(socket, request);
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            if (socket->bytesAvailable() > 0)
                socket->readyRead();
        }
    }

    void processRequest(QTcpSocket *socket, const std::shared_ptr<QByteArray> &request)
    {
        *request += socket->readAll();
        const int headerEnd = request->indexOf("\r\n\r\n");
        if (headerEnd < 0 || socket->property("answered").toBool())
            return;
        const QList<QByteArray> parts = request->left(headerEnd).split('\n')
            .value(0).trimmed().split(' ');
        if (parts.size() < 2) {
            socket->abort();
            return;
        }
        const QString target = QString::fromLatin1(parts.at(1));
        FTargets.append(target);
        const QUrl url(QStringLiteral("http://localhost") + target);
        const QByteArray body = responseBody(url);
        socket->setProperty("answered", true);
        writeResponse(socket, body);
    }

    QByteArray responseBody(const QUrl &url) const
    {
        if (url.path().endsWith(QStringLiteral("/sync")))
            return QJsonDocument(syncResponse()).toJson(QJsonDocument::Compact);
        const QString cursor = QUrlQuery(url).queryItemValue(QStringLiteral("from"));
        return QJsonDocument(backfillResponse(cursor)).toJson(QJsonDocument::Compact);
    }

    QJsonObject syncResponse() const
    {
        const QJsonObject syncRoom{
            {QStringLiteral("timeline"), QJsonObject{
                {QStringLiteral("limited"), true},
                {QStringLiteral("prev_batch"), QStringLiteral("cursor-0")},
                {QStringLiteral("events"), QJsonArray{message(
                    QStringLiteral("$sync-recent"),
                    QStringLiteral("@history-test:example.org"),
                    QStringLiteral("recent message after restart"), 1000)}}}}};
        return {
            {QStringLiteral("next_batch"), QStringLiteral("sync-next")},
            {QStringLiteral("device_one_time_keys_count"),
                QJsonObject{{QStringLiteral("signed_curve25519"), 100}}},
            {QStringLiteral("device_unused_fallback_key_types"),
                QJsonArray{QStringLiteral("signed_curve25519")}},
            {QStringLiteral("rooms"), QJsonObject{
                {QStringLiteral("join"), QJsonObject{
                    {QStringLiteral("!history-backfill:example.org"), syncRoom}}}}}};
    }

    QJsonObject backfillResponse(const QString &cursor) const
    {
        QJsonArray chunk;
        if (cursor == QStringLiteral("cursor-0")) {
            chunk = QJsonArray{message(QStringLiteral("$self-backfilled"),
                QStringLiteral("@history-test:example.org"),
                QStringLiteral("sent on another client"), 900)};
        } else {
            chunk = QJsonArray{
                message(QStringLiteral("$older-backfilled"),
                    QStringLiteral("@history-test:example.org"),
                    QStringLiteral("older own message"), 800),
                message(QStringLiteral("$known-overlap"),
                    QStringLiteral("@peer:example.org"),
                    QStringLiteral("already cached"), 700)};
        }
        const QByteArray endCursor = cursor == QStringLiteral("cursor-0")
            ? QByteArray("cursor-1") : QByteArray("cursor-2");
        return {
            {QStringLiteral("chunk"), chunk},
            {QStringLiteral("end"), QString::fromLatin1(endCursor)}};
    }

    void writeResponse(QTcpSocket *socket, const QByteArray &body) const
    {
        QByteArray response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n";
        response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
        response += "Connection: close\r\n\r\n" + body;
        socket->write(response);
        socket->disconnectFromHost();
    }

    static QJsonObject message(const QString &eventId, const QString &sender,
                               const QString &body, qint64 timestamp)
    {
        return {{QStringLiteral("event_id"), eventId}, {QStringLiteral("sender"), sender},
            {QStringLiteral("type"), QStringLiteral("m.room.message")},
            {QStringLiteral("content"), QJsonObject{{QStringLiteral("msgtype"),
                QStringLiteral("m.text")}, {QStringLiteral("body"), body}}},
            {QStringLiteral("origin_server_ts"), timestamp}};
    }

    QStringList FTargets;
};

bool testLegacyMediaFallback(MatrixNetwork &network, const QString &suffix,
                             const QByteArray &errorCode)
{
    const QByteArray png = syntheticPng();
    if (png.isEmpty()) {
        std::cerr << "could not create synthetic Matrix media PNG\n";
        return false;
    }

    SyntheticMediaServer server(png, errorCode);
    if (!server.listen(QHostAddress::LocalHost, 0)) {
        std::cerr << "could not start synthetic Matrix media server\n";
        return false;
    }

    network.setServerUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
    MatrixNetworkTestAccess::setAccessToken(network, QStringLiteral("synthetic-test-token"));

    MatrixTextEvent event;
    event.eventId = QStringLiteral("$legacy-media-fallback-%1").arg(suffix);
    event.roomId = QStringLiteral("!legacy-media:example.org");
    event.userId = QStringLiteral("@sender:example.org");
    event.content = QStringLiteral("legacy-image.png");
    event.timestamp = QStringLiteral("3000");
    event.eventType = QStringLiteral("m.room.message");
    event.messageType = QStringLiteral("m.image");
    event.metadata.insert(QStringLiteral("url"), QStringLiteral(
        "mxc://media.example.org/fallback-image-id-%1").arg(suffix));
    event.metadata.insert(QStringLiteral("msgtype"), event.messageType);
    event.metadata.insert(QStringLiteral("event_type"), event.eventType);

    bool imageReceived = false;
    BasicMessage hydratedMessage;
    QEventLoop mediaLoop;
    const QMetaObject::Connection receivedConnection = QObject::connect(
        &network, &MatrixNetwork::messageReceived, &mediaLoop,
        [&](const BasicMessage &message) {
            if (message.messageId() != event.eventId)
                return;
            imageReceived = true;
            hydratedMessage = message;
            mediaLoop.quit();
        });

    network.requestImage(event);
    waitForEventLoop(mediaLoop, 3000, [&]() { return imageReceived; });
    QObject::disconnect(receivedConnection);

    const QString mediaPath = QStringLiteral("media.example.org/fallback-image-id-%1").arg(suffix);
    const QStringList expectedPaths = {
        QStringLiteral("/_matrix/client/v1/media/download/") + mediaPath,
        QStringLiteral("/_matrix/media/v3/download/") + mediaPath
    };
    const QStringList actualPaths = server.requestPaths();
    if (actualPaths != expectedPaths) {
        std::cerr << "Matrix image fallback request paths were: ";
        for (const QString &path : actualPaths)
            std::cerr << path.toStdString() << ' ';
        std::cerr << "(expected client/v1 404 then media/v3)\n";
        MatrixNetworkTestAccess::setAccessToken(network, QString());
        return false;
    }

    const QStringList authorization = server.authorizationHeaders();
    if (authorization.size() != 2 ||
        authorization.at(0) != QStringLiteral("Bearer synthetic-test-token") ||
        !authorization.at(1).isEmpty()) {
        std::cerr << "Matrix media fallback used unexpected Authorization headers\n";
        MatrixNetworkTestAccess::setAccessToken(network, QString());
        return false;
    }

    const QVariantMap hydratedMetadata = hydratedMessage.metadata();
    const QImage decoded = hydratedMetadata.value(QStringLiteral("decoded_image")).value<QImage>();
    const QString cachedFile = hydratedMetadata.value(QStringLiteral("file_path")).toString();
    if (!imageReceived || hydratedMetadata.value(QStringLiteral("msgtype")).toString() !=
            QStringLiteral("m.image") ||
        hydratedMetadata.value(QStringLiteral("event_type")).toString() !=
            QStringLiteral("m.room.message") ||
        hydratedMetadata.value(QStringLiteral("image_resource_url")).toString().isEmpty() ||
        !QFileInfo::exists(cachedFile) || decoded.size() != QSize(8, 8) ||
        decoded.pixelColor(0, 0) != QColor(Qt::blue)) {
        std::cerr << "legacy Matrix media response did not hydrate a renderable m.image event\n";
        MatrixNetworkTestAccess::setAccessToken(network, QString());
        return false;
    }

    MatrixNetworkTestAccess::setAccessToken(network, QString());
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir profileDirectory;
    if (!profileDirectory.isValid())
        return fail("could not create temporary Matrix profile directory");

    MatrixNetwork network;
    QThread databaseThread;
    MatrixDatabaseWorker *databaseWorker = new MatrixDatabaseWorker;
    databaseWorker->moveToThread(&databaseThread);
    QObject::connect(&databaseThread, &QThread::finished,
                     databaseWorker, &QObject::deleteLater);
    databaseThread.start();

    auto stopDatabaseWorker = [&]() {
        QMetaObject::invokeMethod(&network, "setDatabaseWorker", Qt::DirectConnection,
                                  Q_ARG(QObject *, static_cast<QObject *>(nullptr)));
        QMetaObject::invokeMethod(databaseWorker, [databaseWorker]() {
            databaseWorker->close();
        }, Qt::BlockingQueuedConnection);
        databaseThread.quit();
        databaseThread.wait();
    };

    bool databaseOpened = false;
    QMetaObject::invokeMethod(databaseWorker, [&]() {
        databaseOpened = databaseWorker->openForAccount(profileDirectory.path(),
            QStringLiteral("https://history.example.org"),
            QStringLiteral("@history-test:example.org"));
    }, Qt::BlockingQueuedConnection);
    if (!databaseOpened) {
        databaseThread.quit();
        databaseThread.wait();
        return fail("could not open temporary Matrix database");
    }

    const QString roomId = QStringLiteral("!history-retry:example.org");
    MatrixTimelineEvent cachedEvent;
    cachedEvent.roomId = roomId;
    cachedEvent.eventId = QStringLiteral("$cached-plaintext");
    cachedEvent.eventType = QStringLiteral("m.room.message");
    cachedEvent.sender = QStringLiteral("@sender:example.org");
    cachedEvent.originTs = 1000;
    cachedEvent.messageType = QStringLiteral("m.text");
    cachedEvent.content = QStringLiteral("cached message");
    bool eventSaved = false;
    QMetaObject::invokeMethod(databaseWorker, [&]() {
        databaseWorker->execute([&](MatrixDatabase &database) {
            eventSaved = database.appendTimelineEvents(roomId, QList<MatrixTimelineEvent>{cachedEvent});
        });
    }, Qt::BlockingQueuedConnection);
    if (!eventSaved) {
        stopDatabaseWorker();
        return fail("could not seed temporary Matrix history");
    }

    const bool workerSet = QMetaObject::invokeMethod(&network, "setDatabaseWorker",
        Qt::DirectConnection, Q_ARG(QObject *, static_cast<QObject *>(databaseWorker)));
    if (!workerSet) {
        stopDatabaseWorker();
        return fail("could not configure Matrix database worker");
    }

    // History recovery is lazy: /sync persists the room cursor but does not
    // fetch history until the user opens the room. Opening fetches one 50-event
    // page, and an explicit scroll fetches the next page with a 30-event limit.
    const QString backfillRoom = QStringLiteral("!history-backfill:example.org");
    MatrixNetwork backfillNetwork;
    QMetaObject::invokeMethod(&backfillNetwork, "setDatabaseWorker", Qt::DirectConnection,
        Q_ARG(QObject *, static_cast<QObject *>(databaseWorker)));
    HistoryBackfillServer backfillServer;
    if (!backfillServer.listen(QHostAddress::LocalHost, 0)) {
        stopDatabaseWorker();
        return fail("could not start local Matrix history fixture");
    }
    backfillNetwork.setServerUrl(QStringLiteral("http://127.0.0.1:%1")
        .arg(backfillServer.serverPort()));
    MatrixNetworkTestAccess::setAccessToken(backfillNetwork, QStringLiteral("synthetic-test-token"));
    MatrixNetworkTestAccess::setUserId(backfillNetwork, QStringLiteral("@history-test:example.org"));
    MatrixNetworkTestAccess::prepareSync(backfillNetwork);
    MatrixTextEvent overlap;
    overlap.roomId = backfillRoom;
    overlap.eventId = QStringLiteral("$known-overlap");
    overlap.userId = QStringLiteral("@peer:example.org");
    overlap.timestamp = QStringLiteral("700");
    overlap.eventType = QStringLiteral("m.room.message");
    overlap.messageType = QStringLiteral("m.text");
    if (!MatrixNetworkTestAccess::mergeMessageEvent(backfillNetwork, overlap, true)) {
        stopDatabaseWorker();
        return fail("could not seed the known Matrix history overlap");
    }

    int backfillSnapshots = 0;
    QObject::connect(&backfillNetwork, &MatrixNetwork::messageHistoryChanged, &app,
        [&](const QString &changedRoom, const QList<MatrixTextEvent> &) {
            if (changedRoom == backfillRoom)
                ++backfillSnapshots;
        });
    bool initialSyncCompleted = false;
    QEventLoop syncLoop;
    QObject::connect(&backfillNetwork, &MatrixNetwork::initialSyncCompleted, &syncLoop,
        [&]() { initialSyncCompleted = true; syncLoop.quit(); });
    backfillNetwork.sync();
    waitForEventLoop(syncLoop, 5000, [&]() { return initialSyncCompleted; });
    QEventLoop drainLoop;
    QTimer::singleShot(50, &drainLoop, &QEventLoop::quit);
    drainLoop.exec();

    const QStringList initialBackfillTargets = backfillServer.targets();
    for (const QString &target : initialBackfillTargets) {
        if (target.contains(QStringLiteral("/messages?"))) {
            stopDatabaseWorker();
            return fail("limited /sync must not fetch conversation history before the room is opened");
        }
    }

    // Simulate opening the conversation: one initial page, capped at 50.
    backfillNetwork.historyBackfill(backfillRoom, 50);
    if (backfillSnapshots != 1) {
        stopDatabaseWorker();
        return fail("opening a room must fetch exactly one initial history page");
    }

    // Simulate the user's first scroll-up request: fetch the next page with a
    // 30-event cap from the opaque cursor returned by the initial page.
    backfillNetwork.historyBackfill(backfillRoom, 30);
    const QList<MatrixTextEvent> backfilledHistory =
        MatrixNetworkTestAccess::history(backfillNetwork, backfillRoom);
    bool sawSelfSent = false;
    bool selfSentMarked = false;
    bool sawOlderSelfSent = false;
    bool sawRecentSyncEvent = false;
    int overlapCount = 0;
    for (const MatrixTextEvent &event : backfilledHistory) {
        sawSelfSent = sawSelfSent || event.eventId == QStringLiteral("$self-backfilled");
        if (event.eventId == QStringLiteral("$self-backfilled"))
            selfSentMarked = event.metadata.value(QStringLiteral("sender_is_self")).toBool();
        sawOlderSelfSent = sawOlderSelfSent || event.eventId == QStringLiteral("$older-backfilled");
        sawRecentSyncEvent = sawRecentSyncEvent || event.eventId == QStringLiteral("$sync-recent");
        overlapCount += event.eventId == QStringLiteral("$known-overlap") ? 1 : 0;
    }
    const QStringList backfillTargets = backfillServer.targets();
    QStringList messagePageTargets;
    for (const QString &target : backfillTargets)
        if (target.contains(QStringLiteral("/messages?")))
            messagePageTargets.append(target);
    if (backfillSnapshots < 2 || !sawSelfSent ||
        !selfSentMarked ||
        !sawOlderSelfSent || !sawRecentSyncEvent || overlapCount != 1 ||
        messagePageTargets.size() != 2 ||
        !QUrl(QStringLiteral("http://localhost") + backfillTargets.value(0))
             .path().endsWith(QStringLiteral("/sync")) ||
        QUrlQuery(QUrl(QStringLiteral("http://localhost") + messagePageTargets.value(0)))
                .queryItemValue(QStringLiteral("from")) != QStringLiteral("cursor-0") ||
        QUrlQuery(QUrl(QStringLiteral("http://localhost") + messagePageTargets.value(1)))
                .queryItemValue(QStringLiteral("from")) != QStringLiteral("cursor-1") ||
        QUrlQuery(QUrl(QStringLiteral("http://localhost") + messagePageTargets.value(0)))
                .queryItemValue(QStringLiteral("limit")) != QStringLiteral("50") ||
        QUrlQuery(QUrl(QStringLiteral("http://localhost") + messagePageTargets.value(1)))
                .queryItemValue(QStringLiteral("limit")) != QStringLiteral("30")) {
        stopDatabaseWorker();
        return fail("Matrix history must fetch one initial 50-event page and only fetch 30 more on scroll");
    }
    MatrixNetworkTestAccess::setAccessToken(backfillNetwork, QString());

    // Destroy the network worker while historyBackfill is inside its nested
    // wait loop. Shutdown aborts the pending reply; the backfill must return
    // without dereferencing either destroyed QObject.
    QTcpServer stalledBackfillServer;
    if (!stalledBackfillServer.listen(QHostAddress::LocalHost, 0)) {
        stopDatabaseWorker();
        return fail("could not start stalled Matrix backfill fixture");
    }
    QObject::connect(&stalledBackfillServer, &QTcpServer::newConnection, &app,
        [&stalledBackfillServer]() {
            while (stalledBackfillServer.hasPendingConnections()) {
                QTcpSocket *socket = stalledBackfillServer.nextPendingConnection();
                QObject::connect(socket, &QTcpSocket::readyRead, socket,
                    [socket]() { socket->readAll(); });
            }
        });
    auto *cancelledBackfillNetwork = new MatrixNetwork;
    cancelledBackfillNetwork->setServerUrl(QStringLiteral("http://127.0.0.1:%1")
        .arg(stalledBackfillServer.serverPort()));
    MatrixNetworkTestAccess::setAccessToken(*cancelledBackfillNetwork,
        QStringLiteral("synthetic-test-token"));
    MatrixNetworkTestAccess::prepareBackfill(*cancelledBackfillNetwork,
        QStringLiteral("!cancelled-backfill:example.org"), QStringLiteral("cursor"));
    QTimer::singleShot(0, &app, [cancelledBackfillNetwork]() {
        cancelledBackfillNetwork->shutdown();
        delete cancelledBackfillNetwork;
    });
    const QList<MatrixTextEvent> cancelledBackfill =
        cancelledBackfillNetwork->historyBackfill(
            QStringLiteral("!cancelled-backfill:example.org"), 50);
    if (!cancelledBackfill.isEmpty()) {
        stopDatabaseWorker();
        return fail("cancelled Matrix backfill unexpectedly returned events");
    }

    if (!testLegacyMediaFallback(network, QStringLiteral("unrecognized"),
                                  QByteArray("M_UNRECOGNIZED")) ||
        !testLegacyMediaFallback(network, QStringLiteral("not-found"),
                                  QByteArray("M_NOT_FOUND"))) {
        stopDatabaseWorker();
        return 1;
    }

    QList<MatrixTimelineEvent> cachedImages;
    QSet<QString> expectedImageIds;
    for (int i = 0; i < 2; ++i) {
        MatrixTimelineEvent event;
        event.roomId = roomId;
        event.eventId = QStringLiteral("$cached-image-%1").arg(i);
        event.eventType = QStringLiteral("m.room.message");
        event.sender = QStringLiteral("@sender:example.org");
        event.originTs = 2000 + i;
        event.messageType = QStringLiteral("m.image");
        event.content = QStringLiteral("photo-%1.png").arg(i);
        const QString mxcUrl = QStringLiteral("mxc://media.example.org/cached-image-%1").arg(i);
        event.metadata.insert(QStringLiteral("url"), mxcUrl);
        if (!seedPng(mediaCachePath(profileDirectory.path(), mxcUrl))) {
            stopDatabaseWorker();
            return fail("could not seed temporary Matrix image cache");
        }
        expectedImageIds.insert(event.eventId);
        cachedImages.append(event);
    }
    MatrixTimelineEvent nonImage = cachedEvent;
    nonImage.eventId = QStringLiteral("$not-an-image");
    cachedImages.append(nonImage);
    MatrixTimelineEvent otherRoomImage = cachedImages.first();
    otherRoomImage.roomId = QStringLiteral("!other:example.org");
    otherRoomImage.eventId = QStringLiteral("$other-room-image");
    cachedImages.append(otherRoomImage);

    QList<BasicMessage> hydratedMessages;
    QEventLoop mediaLoop;
    QObject::connect(&network, &MatrixNetwork::messageReceived, &app,
        [&](const BasicMessage &message) {
            if (expectedImageIds.contains(message.messageId())) {
                hydratedMessages.append(message);
                if (hydratedMessages.size() == expectedImageIds.size())
                    mediaLoop.quit();
            }
        });
    network.requestHistoricalImages(roomId, cachedImages);
    const bool imagesHydrated = waitForEventLoop(mediaLoop, 10000, [&]() {
        return hydratedMessages.size() == expectedImageIds.size();
    });
    if (!imagesHydrated) {
        stopDatabaseWorker();
        return fail("cached historical image requests did not emit both hydrated events");
    }
    QSet<QString> receivedIds;
    for (const BasicMessage &message : hydratedMessages) {
        receivedIds.insert(message.messageId());
        if (!message.metadata().value(QStringLiteral("historical")).toBool() ||
            message.metadata().value(QStringLiteral("decoded_image")).value<QImage>().isNull()) {
            stopDatabaseWorker();
            return fail("historical image event was not marked historical and decoded");
        }
    }
    if (receivedIds != expectedImageIds) {
        stopDatabaseWorker();
        return fail("historical image hydration returned an unexpected event set");
    }

    HistorySignalCounter historyCounter;
    MessageReceivedCounter messageCounter;
    QObject::connect(&network,
        SIGNAL(messageHistoryChanged(QString,QList<MatrixTextEvent>)),
        &historyCounter,
        SLOT(onHistoryChanged(QString,QList<MatrixTextEvent>)));
    QObject::connect(&network,
        SIGNAL(messageReceived(BasicMessage)),
        &messageCounter,
        SLOT(onMessageReceived(BasicMessage)));

    const bool roomActivated = QMetaObject::invokeMethod(&network, "setActiveRoom",
        Qt::DirectConnection, Q_ARG(QString, roomId));
    stopDatabaseWorker();
    if (!roomActivated)
        return fail("could not invoke active-room retry path");
    if (historyCounter.count != 0 || messageCounter.count != 0) {
        std::cerr << "a no-op key retry emitted UI updates: history=" << historyCounter.count
                  << ", messages=" << messageCounter.count << '\n';
        return 1;
    }

    MatrixTextEvent event;
    event.roomId = roomId;
    event.eventId = QStringLiteral("$new-message");
    event.userId = QStringLiteral("@sender:example.org");
    event.eventType = QStringLiteral("m.room.message");
    event.messageType = QStringLiteral("m.text");
    event.content = QStringLiteral("new message");
    event.timestamp = QStringLiteral("1010");
    if (!MatrixNetworkTestAccess::mergeMessageEvent(network, event))
        return fail("could not merge a new Matrix message");
    if (messageCounter.count != 1 || historyCounter.count != 0)
        return fail("a new message should emit one incremental notification, not a room snapshot");
    if (MatrixNetworkTestAccess::mergeMessageEvent(network, event))
        return fail("duplicate Matrix event was unexpectedly merged");
    if (messageCounter.count != 1 || historyCounter.count != 0)
        return fail("duplicate event emitted an unexpected UI notification");

    MatrixTextEvent backfillEvent = event;
    backfillEvent.eventId = QStringLiteral("$backfill-message");
    backfillEvent.timestamp = QStringLiteral("900");
    if (!MatrixNetworkTestAccess::mergeMessageEvent(network, backfillEvent, true))
        return fail("could not merge a history-backfill message");
    if (messageCounter.count != 1 || historyCounter.count != 0)
        return fail("history-backfill merge emitted an incremental live-message notification");

    return 0;
}

#include "matrixhistoryretry_test.moc"
