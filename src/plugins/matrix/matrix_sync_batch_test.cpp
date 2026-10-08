#include "matrix.h"
#include "matrixdatabase.h"
#include "matrixnetwork.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QStringList>

#include <iostream>

class MatrixSyncBatchTestAccess
{
public:
	static void setHistoryLoading(Matrix &matrix, const QString &roomId)
	{
		matrix.FHistoryLoadingRooms.insert(roomId);
	}
	static void queuePendingMessage(Matrix &matrix, const BasicMessage &message)
	{
		matrix.queueOrEmitHistoryMessage(message);
	}
	static void loadCachedHistory(Matrix &matrix, const QString &roomId,
		const QList<MatrixTimelineEvent> &events)
	{
		matrix.onCachedHistoryLoaded(roomId, events);
	}
	static void processCachedHistoryBatch(Matrix &matrix)
	{
		matrix.emitCachedHistoryBatch();
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
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    Matrix matrix;
    MatrixNetwork network;

    QList<MatrixTextEvent> events;
    QStringList expectedIds;
    constexpr int eventCount = 61;
    for (int index = 0; index < eventCount; ++index) {
        MatrixTextEvent event;
        event.roomId = QStringLiteral("!sync-batch:example.invalid");
        event.eventId = QStringLiteral("$sync-%1").arg(index);
        event.userId = QStringLiteral("@sender:example.invalid");
        event.content = QStringLiteral("synthetic sync event %1").arg(index);
        event.timestamp = QString::number(1000 + index);
        event.eventType = QStringLiteral("m.room.message");
        event.messageType = QStringLiteral("m.text");
        events.append(event);
        expectedIds.append(event.eventId);
    }

    int batchSignalCount = 0;
    int perMessageSignalCount = 0;
    QStringList receivedIds;
    QObject::connect(&network, &MatrixNetwork::syncReceived, &network,
        [&batchSignalCount](const QList<MatrixTextEvent> &) { ++batchSignalCount; });
    QObject::connect(&network, &MatrixNetwork::messageReceived, &network,
        [&perMessageSignalCount](const BasicMessage &) { ++perMessageSignalCount; });
    QObject::connect(&network, SIGNAL(syncReceived(QList<MatrixTextEvent>)),
        &matrix, SLOT(onSyncReceived(QList<MatrixTextEvent>)), Qt::DirectConnection);
    QObject::connect(&network, SIGNAL(messageHistoryChanged(QString,QList<MatrixTextEvent>)),
        &matrix, SLOT(onMessageHistoryChanged(QString,QList<MatrixTextEvent>)), Qt::DirectConnection);
    QObject::connect(&matrix, &Matrix::protocolMessageReceived, &matrix,
        [&receivedIds](const BasicMessage &message) { receivedIds.append(message.messageId()); });

    const bool dispatched = QMetaObject::invokeMethod(&network, "dispatchSyncEventBatch",
        Qt::DirectConnection, Q_ARG(QList<MatrixTextEvent>, events));
    if (!check(dispatched, "sync events are dispatched through the single batch signal"))
        return 1;
    if (!check(batchSignalCount == 1 && perMessageSignalCount == 0,
            "one sync batch does not enqueue one cross-thread message signal per event"))
        return 1;

    int turns = 0;
    while (receivedIds.size() < eventCount && turns < eventCount) {
        const int previousCount = receivedIds.size();
        if (!check(QMetaObject::invokeMethod(&matrix, "processNextProtocolEventBatch",
                Qt::DirectConnection),
                "Matrix exposes its bounded sync-event batch processor"))
            return 1;
        const int processedThisTurn = receivedIds.size() - previousCount;
        if (!check(processedThisTurn > 0 && processedThisTurn <= 25,
                "each UI processing turn handles a bounded number of sync events"))
            return 1;
        ++turns;
    }

    if (!check(receivedIds == expectedIds,
            "batched sync delivery preserves every message exactly once and in order"))
        return 1;

    QList<MatrixTextEvent> historyEvents;
    QStringList expectedHistoryIds;
    for (int index = 0; index < eventCount; ++index) {
        MatrixTextEvent event;
        event.roomId = QStringLiteral("!sync-batch:example.invalid");
        event.eventId = QStringLiteral("$history-%1").arg(index);
        event.userId = QStringLiteral("@sender:example.invalid");
        event.content = QStringLiteral("synthetic history event %1").arg(index);
        event.timestamp = QString::number(2000 + index);
        event.eventType = QStringLiteral("m.room.message");
        event.messageType = QStringLiteral("m.text");
        if (index == 5 || index == 6) {
            event.metadata.insert(QStringLiteral("outer_event_type"),
                QStringLiteral("m.room.encrypted"));
            event.metadata.insert(QStringLiteral("decryption_status"),
                index == 5 ? QStringLiteral("failed") : QStringLiteral("decrypted"));
        }
        historyEvents.append(event);
        if (index != 5)
            expectedHistoryIds.append(event.eventId);
    }

    const int historyStart = receivedIds.size();
    network.messageHistoryChanged(QStringLiteral("!sync-batch:example.invalid"), historyEvents);
    turns = 0;
    while (receivedIds.size() - historyStart < expectedHistoryIds.size() && turns < eventCount) {
        const int previousCount = receivedIds.size();
        if (!check(QMetaObject::invokeMethod(&matrix, "processNextProtocolEventBatch",
                Qt::DirectConnection),
                "Matrix exposes its bounded history-event batch processor"))
            return 1;
        const int processedThisTurn = receivedIds.size() - previousCount;
        if (!check(processedThisTurn > 0 && processedThisTurn <= 25,
                "each UI processing turn handles a bounded number of history events"))
            return 1;
        ++turns;
    }

    if (!check(receivedIds.mid(historyStart) == expectedHistoryIds,
            "batched history preserves filtering, ordering and exactly-once delivery"))
        return 1;

    const QString pendingRoom = QStringLiteral("!pending-batch:example.invalid");
    MatrixSyncBatchTestAccess::setHistoryLoading(matrix, pendingRoom);
    MatrixTimelineEvent cachedEvent;
    cachedEvent.roomId = pendingRoom;
    cachedEvent.eventId = QStringLiteral("$cached-first");
    cachedEvent.sender = QStringLiteral("@sender:example.invalid");
    cachedEvent.originTs = 3000;
    cachedEvent.eventType = QStringLiteral("m.room.message");
    cachedEvent.messageType = QStringLiteral("m.text");
    cachedEvent.content = QStringLiteral("cached event");
    MatrixSyncBatchTestAccess::loadCachedHistory(matrix, pendingRoom, {cachedEvent});

    QStringList expectedPendingIds{cachedEvent.eventId};
    for (int index = 0; index < 70; ++index) {
        const QString id = QStringLiteral("$pending-%1").arg(index);
        BasicMessage message(id, pendingRoom, QStringLiteral("@sender:example.invalid"),
            QString(), QStringLiteral("pending event %1").arg(index),
            QDateTime::fromMSecsSinceEpoch(4000 + index), QStringLiteral("matrix"),
            BasicMessage::Incoming);
        MatrixSyncBatchTestAccess::queuePendingMessage(matrix, message);
        expectedPendingIds.append(id);
    }

    QStringList historyLoadedRooms;
    QObject::connect(&matrix, &Matrix::protocolHistoryLoaded, &matrix,
        [&historyLoadedRooms](const QString &roomId) { historyLoadedRooms.append(roomId); });
    const int pendingStart = receivedIds.size();
    MatrixSyncBatchTestAccess::processCachedHistoryBatch(matrix);
    const int firstPendingTurnCount = receivedIds.size() - pendingStart;
    if (!check(firstPendingTurnCount > 0 && firstPendingTurnCount <= 25,
            "finishing cached history does not synchronously flush an unbounded pending backlog"))
        return 1;

    turns = 0;
    while (receivedIds.size() - pendingStart < expectedPendingIds.size() && turns < 10) {
        const int previousCount = receivedIds.size();
        MatrixSyncBatchTestAccess::processCachedHistoryBatch(matrix);
        const int processedThisTurn = receivedIds.size() - previousCount;
        if (!check(processedThisTurn > 0 && processedThisTurn <= 25,
                "cached and pending history share bounded processing turns"))
            return 1;
        ++turns;
    }
    if (!check(receivedIds.mid(pendingStart) == expectedPendingIds &&
            historyLoadedRooms == QStringList{pendingRoom},
            "cached history completion preserves order and signals once after pending delivery"))
        return 1;

    std::cout << "PASS: Matrix sync events are delivered in bounded ordered batches\n";
    return 0;
}
