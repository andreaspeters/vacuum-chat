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
#include <QSet>
#include <QMetaObject>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <iostream>

class MatrixNetworkTestAccess
{
public:
    static bool mergeMessageEvent(MatrixNetwork &network, const MatrixTextEvent &event,
                                  bool fromHistoryBackfill = false)
    {
        return network.mergeMessageEvent(event, fromHistoryBackfill);
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
}

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
            eventSaved = database.saveTimeline(roomId, QList<MatrixTimelineEvent>{cachedEvent});
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
    QTimer mediaTimeout;
    mediaTimeout.setSingleShot(true);
    QObject::connect(&network, &MatrixNetwork::messageReceived, &app,
        [&](const BasicMessage &message) {
            if (expectedImageIds.contains(message.messageId())) {
                hydratedMessages.append(message);
                if (hydratedMessages.size() == expectedImageIds.size())
                    mediaLoop.quit();
            }
        });
    QObject::connect(&mediaTimeout, &QTimer::timeout, &mediaLoop, &QEventLoop::quit);
    mediaTimeout.start(10000);
    network.requestHistoricalImages(roomId, cachedImages);
    if (hydratedMessages.size() != expectedImageIds.size())
        mediaLoop.exec();
    mediaTimeout.stop();
    if (hydratedMessages.size() != expectedImageIds.size()) {
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
