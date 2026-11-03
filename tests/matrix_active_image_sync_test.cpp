#include "matrix.h"
#include "matrixnetwork.h"

#include <QCoreApplication>
#include <iostream>

class MatrixNetworkTestAccess
{
public:
    static void setActiveRoom(MatrixNetwork &network, const QString &roomId)
    {
        network.FActiveRoomId = roomId;
    }

    static void dispatchSyncEvents(MatrixNetwork &network, const QList<MatrixTextEvent> &events)
    {
        network.dispatchSyncEventBatch(events);
    }
};

class MatrixSyncBatchTestAccess
{
public:
    static void receiveSyncBatch(Matrix &matrix, const QList<MatrixTextEvent> &events)
    {
        matrix.onSyncReceived(events);
    }

    static void processNextBatch(Matrix &matrix)
    {
        matrix.processNextProtocolEventBatch();
    }
};

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    Matrix matrix;

    MatrixTextEvent image;
    image.roomId = QStringLiteral("!image-room:example.invalid");
    image.eventId = QStringLiteral("$pending-image:example.invalid");
    image.userId = QStringLiteral("@sender:example.invalid");
    image.content = QStringLiteral("encrypted.png");
    image.timestamp = QStringLiteral("1700000000000");
    image.eventType = QStringLiteral("m.room.message");
    image.messageType = QStringLiteral("m.image");
    image.metadata.insert(QStringLiteral("msgtype"), QStringLiteral("m.image"));
    image.metadata.insert(QStringLiteral("url"), QStringLiteral("mxc://media.example.invalid/image"));

    MatrixNetwork network;
    MatrixNetworkTestAccess::setActiveRoom(network, image.roomId);
    int receivedCount = 0;
    BasicMessage received;
    QObject::connect(&matrix, &Matrix::protocolMessageReceived, &matrix,
        [&receivedCount, &received](const BasicMessage &message) {
            ++receivedCount;
            received = message;
        });
    QObject::connect(&network, &MatrixNetwork::syncReceived, &matrix,
        [&matrix](const QList<MatrixTextEvent> &events) {
            MatrixSyncBatchTestAccess::receiveSyncBatch(matrix, events);
        });

    MatrixNetworkTestAccess::dispatchSyncEvents(network, {image});
    MatrixSyncBatchTestAccess::processNextBatch(matrix);

    if (receivedCount != 1) {
        std::cerr << "an active image must reach the UI while its media fetch is pending\n";
        return 1;
    }
    if (received.messageId() != image.eventId ||
        received.metadata().value(QStringLiteral("msgtype")).toString() != QStringLiteral("m.image")) {
        std::cerr << "the pending image event identity and media type must be preserved\n";
        return 1;
    }
    return 0;
}
