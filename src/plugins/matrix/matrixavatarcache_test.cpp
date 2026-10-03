#include "matrixnetwork.h"
#include "matrixdatabaseworker.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
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
                    const QByteArray body = isAvatar ? avatarBytes : QByteArrayLiteral("{}");
                    const QByteArray contentType = isAvatar
                        ? QByteArrayLiteral("image/png") : QByteArrayLiteral("application/json");
                    QByteArray response = QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: ") +
                        contentType + QByteArrayLiteral("\r\nContent-Length: ") +
                        QByteArray::number(body.size()) +
                        QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body;
                    socket->write(response);
                    socket->flush();
                    socket->disconnectFromHost();
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

    const bool fetchedFromServer = avatarRequests == 1;
    const bool gotImage = imageReceived && !receivedImage.isNull();
    QFile updatedCache(cachePath);
    const bool cacheReplaced = updatedCache.open(QIODevice::ReadOnly) &&
        !QImage::fromData(updatedCache.readAll()).isNull();
    imageReceived = false;
    receivedImage = QImage();
    network.requestAvatar(QStringLiteral("room-avatar-key"), mxcUrl);
    const bool warmCacheLoaded = imageReceived && !receivedImage.isNull();
    const bool warmCacheAvoidedNetwork = avatarRequests == 1;
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

    std::cout << "Matrix corrupt-avatar fallback and warm-cache test passed\n";
    return 0;
}
