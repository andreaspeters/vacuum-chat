#include "matrixdatabaseworker.h"

namespace
{
QList<MatrixCachedRoom> loadCachedRooms(MatrixDatabase &database,
                                        bool includeTimelineBoundary)
{
    QList<MatrixCachedRoom> result;
    for (const MatrixStoredRoom &room : database.roomStates()) {
        MatrixCachedRoom cached;
        cached.room = room;
        cached.members = database.roomMembers(room.roomId);
        if (includeTimelineBoundary)
            database.loadRoomTimelineBoundary(room.roomId, cached.previousBatch,
                                              cached.limited);
        result.append(cached);
    }
    return result;
}
}

MatrixDatabaseWorker::MatrixDatabaseWorker(QObject *parent)
    : QObject(parent)
{
}

void MatrixDatabaseWorker::execute(const std::function<void(MatrixDatabase &)> &operation)
{
    if (operation)
        operation(FDatabase);
}

bool MatrixDatabaseWorker::openForAccount(const QString &profileDirectory,
                                          const QString &serverUrl,
                                          const QString &userId)
{
    FDatabase.setProfileDirectory(profileDirectory);
    return FDatabase.openForAccount(serverUrl, userId);
}

bool MatrixDatabaseWorker::persistSyncBatch(const QList<MatrixTimelineEvent> &events,
                                            const QString &nextBatch)
{
    return FDatabase.persistSyncBatch(events, nextBatch);
}

QList<MatrixCachedRoom> MatrixDatabaseWorker::loadPersistedRooms()
{
    return loadCachedRooms(FDatabase, true);
}

QSet<QString> MatrixDatabaseWorker::directRooms() const
{
    return FDatabase.directRooms();
}

QList<MatrixTimelineEvent> MatrixDatabaseWorker::events(const QString &roomId) const
{
    return FDatabase.getEvents(roomId);
}

QString MatrixDatabaseWorker::nextBatch() const
{
    return FDatabase.nextBatch();
}

QString MatrixDatabaseWorker::syncFilter() const
{
    return FDatabase.syncFilter();
}

bool MatrixDatabaseWorker::removeSyncFilter()
{
    return FDatabase.removeSyncFilter();
}

bool MatrixDatabaseWorker::clearNextBatch()
{
    return FDatabase.setNextBatch(QString());
}

bool MatrixDatabaseWorker::checkAndSaveMegolmMessageIndex(const QString &sessionId,
                                                          quint32 messageIndex,
                                                          const QString &eventId,
                                                          bool &replayDetected)
{
    return FDatabase.checkAndSaveMegolmMessageIndex(sessionId, messageIndex, eventId,
                                                    replayDetected);
}

void MatrixDatabaseWorker::close()
{
    FDatabase.close();
}

void MatrixDatabaseWorker::checkCompleteCrossSigningKeys(const QString &userId)
{
    emit completeCrossSigningKeysChecked(userId, FDatabase.hasCompleteCrossSigningKeys(userId));
}

void MatrixDatabaseWorker::loadRooms(const QString &profileDirectory,
                                     const QString &serverUrl,
                                     const QString &userId)
{
    if (!openForAccount(profileDirectory, serverUrl, userId)) {
        emit loadFailed(QStringLiteral("Matrix SQLite database could not be opened"));
        return;
    }

    const QList<MatrixCachedRoom> result = loadCachedRooms(FDatabase, false);
    emit roomsLoaded(result);
}

void MatrixDatabaseWorker::loadHistory(const QString &profileDirectory,
                                       const QString &serverUrl,
                                       const QString &userId,
                                       const QString &roomId)
{
    MatrixHistoryPageResult page;
    if (!loadHistoryPageForAccount(profileDirectory, serverUrl, userId, roomId, 30,
            0, QString(), page)) {
        emit historyLoadFailed(roomId,
            QStringLiteral("Matrix SQLite database could not be opened for history"));
        return;
    }
    if (!page.success) {
        emit historyLoadFailed(roomId,
            QStringLiteral("Matrix SQLite database could not load history page"));
        return;
    }
    emit historyLoaded(roomId, page.events);
}

void MatrixDatabaseWorker::loadHistoryPage(const QString &profileDirectory,
                                           const QString &serverUrl,
                                           const QString &userId,
                                           const QString &roomId, int limit,
                                           qint64 beforeOriginTs,
                                           const QString &beforeEventId)
{
    MatrixHistoryPageResult page;
    if (!loadHistoryPageForAccount(profileDirectory, serverUrl, userId, roomId, limit,
            beforeOriginTs, beforeEventId, page))
        page = MatrixHistoryPageResult();
    emit historyPageLoaded(roomId, page);
}

bool MatrixDatabaseWorker::loadHistoryPageForAccount(const QString &profileDirectory,
                                                     const QString &serverUrl,
                                                     const QString &userId,
                                                     const QString &roomId, int limit,
                                                     qint64 beforeOriginTs,
                                                     const QString &beforeEventId,
                                                     MatrixHistoryPageResult &page)
{
    if (!openForAccount(profileDirectory, serverUrl, userId))
        return false;
    page = FDatabase.loadHistoryPage(roomId, limit, beforeOriginTs, beforeEventId);
    return true;
}
