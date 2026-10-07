#include "matrixdatabaseworker.h"



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
    QList<MatrixCachedRoom> result;
    for (const MatrixStoredRoom &room : FDatabase.roomStates()) {
        MatrixCachedRoom cached;
        cached.room = room;
        cached.members = FDatabase.roomMembers(room.roomId);
        FDatabase.loadRoomTimelineBoundary(room.roomId, cached.previousBatch, cached.limited);
        result.append(cached);
    }
    return result;
}

QSet<QString> MatrixDatabaseWorker::directRooms() const
{
    return FDatabase.directRooms();
}

QStringList MatrixDatabaseWorker::roomIds() const
{
    return FDatabase.roomIds();
}

QList<MatrixTimelineEvent> MatrixDatabaseWorker::events(const QString &roomId) const
{
    return FDatabase.getEvents(roomId);
}

QMap<QString, QByteArray> MatrixDatabaseWorker::deviceKeysForUser(const QString &userId) const
{
    return FDatabase.loadDeviceKeysForUser(userId);
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

bool MatrixDatabaseWorker::beginSyncMetadataBatch() { return FDatabase.beginSyncMetadataBatch(); }
bool MatrixDatabaseWorker::commitSyncMetadataBatch() { return FDatabase.commitSyncMetadataBatch(); }
void MatrixDatabaseWorker::rollbackSyncMetadataBatch() { FDatabase.rollbackSyncMetadataBatch(); }
bool MatrixDatabaseWorker::loadRoomTimelineBoundary(const QString &roomId, QString &prevBatch,
                                                    bool &limited) const
{
    return FDatabase.loadRoomTimelineBoundary(roomId, prevBatch, limited);
}
bool MatrixDatabaseWorker::saveRoomType(const QString &roomId, const QString &roomType,
                                        const QString &roomVersion)
{
    return FDatabase.saveRoomType(roomId, roomType, roomVersion);
}
bool MatrixDatabaseWorker::saveRoomMember(const QString &roomId, const QString &userId,
                                          const QString &membership, const QString &displayName,
                                          const QString &avatarUrl, const QString &eventId)
{
    return FDatabase.saveRoomMember(roomId, userId, membership, displayName, avatarUrl, eventId);
}
bool MatrixDatabaseWorker::saveRoomMemberHistory(const QString &roomId, const QString &userId,
                                                 const QString &membership, const QString &displayName,
                                                 const QString &avatarUrl, qint64 validFrom,
                                                 const QString &eventId)
{
    return FDatabase.saveRoomMemberHistory(roomId, userId, membership, displayName, avatarUrl,
                                           validFrom, eventId);
}
bool MatrixDatabaseWorker::saveReceipt(const QString &roomId, const QString &userId,
                                       const QString &eventId, const QString &receiptType,
                                       qint64 timestamp)
{
    return FDatabase.saveReceipt(roomId, userId, eventId, receiptType, timestamp);
}
bool MatrixDatabaseWorker::saveRoomTimelineBoundary(const QString &roomId, const QString &prevBatch,
                                                    bool limited)
{
    return FDatabase.saveRoomTimelineBoundary(roomId, prevBatch, limited);
}
bool MatrixDatabaseWorker::saveRoomAccountData(const QString &roomId, const QString &eventType,
                                               const QByteArray &contentJson)
{
    return FDatabase.saveRoomAccountData(roomId, eventType, contentJson);
}
bool MatrixDatabaseWorker::saveRoomState(const QString &roomId, const QString &name,
                                         const QString &topic, const QString &avatarUrl,
                                         const QString &membership, bool isDirect, bool isEncrypted,
                                         const QString &replacementRoomId, const QString &prevBatch)
{
    return FDatabase.saveRoomState(roomId, name, topic, avatarUrl, membership, isDirect,
                                   isEncrypted, replacementRoomId, prevBatch);
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
    FDatabase.setProfileDirectory(profileDirectory);
    if (!FDatabase.openForAccount(serverUrl, userId)) {
        emit loadFailed(QStringLiteral("Matrix SQLite database could not be opened"));
        return;
    }

    QList<MatrixCachedRoom> result;
    for (const MatrixStoredRoom &room : FDatabase.roomStates()) {
        MatrixCachedRoom cached;
        cached.room = room;
        cached.members = FDatabase.roomMembers(room.roomId);
        result.append(cached);
    }
    emit roomsLoaded(result);
}

void MatrixDatabaseWorker::loadHistory(const QString &profileDirectory,
                                       const QString &serverUrl,
                                       const QString &userId,
                                       const QString &roomId)
{
    FDatabase.setProfileDirectory(profileDirectory);
    if (!FDatabase.openForAccount(serverUrl, userId)) {
        emit historyLoadFailed(roomId,
            QStringLiteral("Matrix SQLite database could not be opened for history"));
        return;
    }
    emit historyLoaded(roomId, FDatabase.getEvents(roomId));
}

void MatrixDatabaseWorker::loadHistoryPage(const QString &profileDirectory,
                                           const QString &serverUrl,
                                           const QString &userId,
                                           const QString &roomId, int limit,
                                           qint64 beforeOriginTs,
                                           const QString &beforeEventId)
{
    FDatabase.setProfileDirectory(profileDirectory);
    if (!FDatabase.openForAccount(serverUrl, userId)) {
        emit historyPageLoaded(roomId, MatrixHistoryPageResult());
        return;
    }

    emit historyPageLoaded(roomId, FDatabase.loadHistoryPage(
        roomId, limit, beforeOriginTs, beforeEventId));
}
