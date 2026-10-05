#include "matrixnetwork.h"
#include "matrixdatabaseworker.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QNetworkProxy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <iostream>
#include <memory>

namespace {
struct MediaRequestStats
{
    int active = 0;
    int maximum = 0;
    QList<qint64> starts;
};

int fail(const char *message)
{
    std::cerr << message << '\n';
    return 1;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("VacuumTests"));
    QCoreApplication::setApplicationName(QStringLiteral("MatrixAvatarCacheTests"));
    QStandardPaths::setTestModeEnabled(true);
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);

    QTemporaryDir profileDirectory;
    if (!profileDirectory.isValid())
        return fail("could not create temporary Matrix profile directory");

    QByteArray avatarBytes;
    QImage source(8, 8, QImage::Format_ARGB32);
    source.fill(Qt::blue);
    QBuffer imageBuffer(&avatarBytes);
    if (!imageBuffer.open(QIODevice::WriteOnly) || !source.save(&imageBuffer, "PNG"))
        return fail("could not create synthetic avatar image");

    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0))
        return fail("could not start local Matrix media fixture");
    int avatarRequests = 0;
    const auto mediaStats = std::make_shared<MediaRequestStats>();
    QElapsedTimer serverMediaClock;
    serverMediaClock.start();
    QObject::connect(&server, &QTcpServer::newConnection, &app, [&]() {
        while (server.hasPendingConnections()) {
            QTcpSocket *socket = server.nextPendingConnection();
            const auto requestData = std::make_shared<QByteArray>();
            const auto answered = std::make_shared<bool>(false);
            QObject::connect(socket, &QTcpSocket::readyRead, &app,
                [&, socket, requestData, answered, avatarBytes]() {
                    *requestData += socket->readAll();
                    const qsizetype headersEnd = requestData->indexOf("\r\n\r\n");
                    if (*answered || headersEnd < 0)
                        return;
                    *answered = true;
                    const QByteArray requestLine = requestData->left(requestData->indexOf("\r\n"));
                    const QByteArray path = requestLine.split(' ').value(1);
                    const bool isAvatar = path.startsWith(
                        "/_matrix/client/v1/media/download/media.example.org/");
                    if (isAvatar)
                        ++avatarRequests;
                    const bool isBurstMedia = path.contains("/media-burst-");
                    if (isBurstMedia) {
                        ++mediaStats->active;
                        mediaStats->maximum = qMax(mediaStats->maximum, mediaStats->active);
                        mediaStats->starts.append(serverMediaClock.elapsed());
                    }
                    const QByteArray body = isAvatar ? avatarBytes : QByteArrayLiteral("{}");
                    const QByteArray contentType = isAvatar
                        ? QByteArrayLiteral("image/png") : QByteArrayLiteral("application/json");
                    QByteArray response = QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: ") +
                        contentType + QByteArrayLiteral("\r\nContent-Length: ") +
                        QByteArray::number(body.size()) +
                        QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body;
                    if (isBurstMedia) {
                        QTimer::singleShot(6000, socket, [mediaStats, socket, response]() {
                            --mediaStats->active;
                            socket->write(response);
                            socket->flush();
                            socket->disconnectFromHost();
                        });
                    } else {
                        socket->write(response);
                        socket->flush();
                        socket->disconnectFromHost();
                    }
                });
        }
    });

    QThread databaseThread;
    MatrixDatabaseWorker *databaseWorker = new MatrixDatabaseWorker;
    databaseWorker->moveToThread(&databaseThread);
    QObject::connect(&databaseThread, &QThread::finished,
                     databaseWorker, &QObject::deleteLater);
    databaseThread.start();

    MatrixNetwork network;
    network.setDatabaseWorker(databaseWorker);
    network.setDatabaseProfileDirectory(profileDirectory.path());
    network.setServerUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
    QObject::connect(&network, &MatrixNetwork::loginError, &app,
        [](const QString &error) { std::cerr << "login initialization failed: " << error.toStdString() << '\n'; });
    network.loginWithAccessToken(QStringLiteral("@avatar-test:example.org"),
                                 QStringLiteral("synthetic-test-token"),
                                 QStringLiteral("AVATARTEST"));
    if (!network.isLoggedIn()) {
        network.shutdown();
        QMetaObject::invokeMethod(databaseWorker, "close", Qt::BlockingQueuedConnection);
        network.setDatabaseWorker(nullptr);
        databaseThread.quit();
        databaseThread.wait();
        return fail("Matrix test session could not initialize");
    }

    const QString mxcUrl = QStringLiteral("mxc://media.example.org/avatar-id");
    const QString cacheDirectory = profileDirectory.path() + QStringLiteral("/avatars");
    if (!QDir().mkpath(cacheDirectory)) {
        network.shutdown();
        QMetaObject::invokeMethod(databaseWorker, "close", Qt::BlockingQueuedConnection);
        network.setDatabaseWorker(nullptr);
        databaseThread.quit();
        databaseThread.wait();
        return fail("could not create temporary avatar cache directory");
    }
    const QString cachePath = cacheDirectory + QLatin1Char('/') +
        QString::fromLatin1(QCryptographicHash::hash(mxcUrl.toUtf8(), QCryptographicHash::Sha256).toHex()) +
        QStringLiteral(".bin");
    QFile corruptCache(cachePath);
    if (!corruptCache.open(QIODevice::WriteOnly) ||
        corruptCache.write(QByteArrayLiteral("corrupted cached avatar")) < 0) {
        network.shutdown();
        QMetaObject::invokeMethod(databaseWorker, "close", Qt::BlockingQueuedConnection);
        network.setDatabaseWorker(nullptr);
        databaseThread.quit();
        databaseThread.wait();
        return fail("could not seed corrupt avatar cache file");
    }
    corruptCache.close();

    bool imageReceived = false;
    QImage receivedImage;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&network, &MatrixNetwork::avatarImageReceived, &app,
        [&](const QString &key, const QImage &image) {
            if (key == QStringLiteral("room-avatar-key")) {
                imageReceived = true;
                receivedImage = image;
                loop.quit();
            }
        });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(5000);
    network.requestAvatar(QStringLiteral("room-avatar-key"), mxcUrl);
    if (!imageReceived)
        loop.exec();
    timeout.stop();

    const bool fetchedFromServer = avatarRequests == 1;
    const bool gotImage = imageReceived && !receivedImage.isNull();
    QFile updatedCache(cachePath);
    const bool cacheReplaced = updatedCache.open(QIODevice::ReadOnly) &&
        !QImage::fromData(updatedCache.readAll()).isNull();
    imageReceived = false;
    receivedImage = QImage();
    QEventLoop warmCacheLoop;
    QTimer warmCacheTimeout;
    warmCacheTimeout.setSingleShot(true);
    QObject::connect(&warmCacheTimeout, &QTimer::timeout, &warmCacheLoop, &QEventLoop::quit);
    warmCacheTimeout.start(2000);
    network.requestAvatar(QStringLiteral("room-avatar-key"), mxcUrl);
    if (!imageReceived)
        warmCacheLoop.exec();
    warmCacheTimeout.stop();
    const bool warmCacheLoaded = imageReceived && !receivedImage.isNull();
    const bool warmCacheAvoidedNetwork = avatarRequests == 1;

    const QString mediaDirectory = profileDirectory.path() + QStringLiteral("/media");
    if (!QDir().mkpath(mediaDirectory)) {
        network.shutdown();
        QMetaObject::invokeMethod(databaseWorker, "close", Qt::BlockingQueuedConnection);
        network.setDatabaseWorker(nullptr);
        databaseThread.quit();
        databaseThread.wait();
        return fail("could not create temporary Matrix media cache directory");
    }
    MatrixTextEvent firstMedia;
    firstMedia.eventId = QStringLiteral("$media-one");
    firstMedia.roomId = QStringLiteral("!avatar-test:example.org");
    firstMedia.messageType = QStringLiteral("m.image");
    firstMedia.timestamp = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QString firstMediaUrl = QStringLiteral("mxc://media.example.org/media-one");
    firstMedia.metadata.insert(QStringLiteral("url"), firstMediaUrl);
    MatrixTextEvent secondMedia = firstMedia;
    secondMedia.eventId = QStringLiteral("$media-two");
    const QString secondMediaUrl = QStringLiteral("mxc://media.example.org/media-two");
    secondMedia.metadata.insert(QStringLiteral("url"), secondMediaUrl);
    for (const QString &url : {firstMediaUrl, secondMediaUrl}) {
        const QString path = mediaDirectory + QLatin1Char('/') +
            QString::fromLatin1(QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha256).toHex()) +
            QStringLiteral(".bin");
        QFile mediaCache(path);
        if (!mediaCache.open(QIODevice::WriteOnly) || mediaCache.write(avatarBytes) != avatarBytes.size()) {
            network.shutdown();
            QMetaObject::invokeMethod(databaseWorker, "close", Qt::BlockingQueuedConnection);
            network.setDatabaseWorker(nullptr);
            databaseThread.quit();
            databaseThread.wait();
            return fail("could not seed cached Matrix media files");
        }
    }
    QMap<QString, qint64> mediaImageTimes;
    QElapsedTimer mediaClock;
    mediaClock.start();
    QEventLoop mediaLoop;
    QTimer mediaTimeout;
    mediaTimeout.setSingleShot(true);
    QObject::connect(&network, &MatrixNetwork::messageReceived, &app,
        [&](const BasicMessage &message) {
            if (message.messageId() == firstMedia.eventId || message.messageId() == secondMedia.eventId)
                mediaImageTimes.insert(message.messageId(), mediaClock.elapsed());
            if (mediaImageTimes.size() == 2)
                mediaLoop.quit();
        });
    QObject::connect(&mediaTimeout, &QTimer::timeout, &mediaLoop, &QEventLoop::quit);
    mediaTimeout.start(6000);
    network.requestImage(firstMedia);
    network.requestImage(secondMedia);
    if (mediaImageTimes.size() < 2)
        mediaLoop.exec();
    const bool mediaLoadsArePaced = mediaImageTimes.size() == 2 &&
        mediaImageTimes.value(secondMedia.eventId) - mediaImageTimes.value(firstMedia.eventId) >= 900;

    QEventLoop schedulerCooldown;
    QTimer::singleShot(1100, &schedulerCooldown, &QEventLoop::quit);
    schedulerCooldown.exec();
    const int firstBurstRequest = mediaStats->starts.size();
    QList<MatrixTextEvent> burstEvents;
    for (int i = 0; i < 5; ++i) {
        MatrixTextEvent event;
        event.eventId = QStringLiteral("$media-burst-%1").arg(i);
        event.roomId = firstMedia.roomId;
        event.messageType = QStringLiteral("m.image");
        event.timestamp = firstMedia.timestamp;
        event.metadata.insert(QStringLiteral("url"),
            QStringLiteral("mxc://media.example.org/media-burst-%1").arg(i));
        burstEvents.append(event);
    }
    for (const MatrixTextEvent &event : burstEvents)
        network.requestImage(event);
    QEventLoop burstWindow;
    QTimer burstWindowTimeout;
    burstWindowTimeout.setSingleShot(true);
    QObject::connect(&burstWindowTimeout, &QTimer::timeout, &burstWindow, &QEventLoop::quit);
    burstWindowTimeout.start(3600);
    burstWindow.exec();
    const int burstStarted = mediaStats->starts.size() - firstBurstRequest;
    bool burstPaced = burstStarted == 4;
    for (int i = firstBurstRequest + 1; i < mediaStats->starts.size(); ++i)
        if (mediaStats->starts.at(i) - mediaStats->starts.at(i - 1) < 900)
            burstPaced = false;
    const bool burstConcurrencyLimited = mediaStats->maximum <= 4 && mediaStats->active <= 4;

    network.shutdown();
    QMetaObject::invokeMethod(databaseWorker, "close", Qt::BlockingQueuedConnection);
    network.setDatabaseWorker(nullptr);
    databaseThread.quit();
    databaseThread.wait();

    if (!fetchedFromServer)
        return fail("corrupt cached avatar did not fall back to the Matrix media endpoint");
    if (!gotImage)
        return fail("fallback response did not emit a decoded avatar image");
    if (!cacheReplaced || !warmCacheLoaded || !warmCacheAvoidedNetwork)
        return fail("replacement avatar cache was not served without a second media request");
    if (!mediaLoadsArePaced)
        return fail("cached Matrix media loads were not globally paced to one image per second");
    if (!burstPaced)
        return fail("Matrix media requests did not start at most once per second across the burst");
    if (!burstConcurrencyLimited)
        return fail("Matrix media requests exceeded four simultaneous active image loads");

    std::cout << "Matrix corrupt-avatar fallback and warm-cache test passed\n";
    return 0;
}
