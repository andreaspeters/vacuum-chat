#ifndef MATRIXDATABASEWORKER_H
#define MATRIXDATABASEWORKER_H

#include "matrixdatabase.h"
#include <QObject>
#include <QList>
#include <functional>

struct MatrixCachedRoom
{
    MatrixStoredRoom room;
    QList<MatrixStoredMember> members;
    QString previousBatch;
    bool limited = false;
};

Q_DECLARE_METATYPE(MatrixCachedRoom)
Q_DECLARE_METATYPE(QList<MatrixCachedRoom>)

class MatrixDatabaseWorker : public QObject
{
    Q_OBJECT
public:
    explicit MatrixDatabaseWorker(QObject *parent = nullptr);

public slots:
    void execute(const std::function<void(MatrixDatabase &)> &operation);
    bool openForAccount(const QString &profileDirectory, const QString &serverUrl,
                        const QString &userId);
    bool persistSyncBatch(const QList<MatrixTimelineEvent> &events,
                          const QString &nextBatch);
    QList<MatrixCachedRoom> loadPersistedRooms();
    QSet<QString> directRooms() const;
    QList<MatrixTimelineEvent> events(const QString &roomId) const;
    QString nextBatch() const;
    QString syncFilter() const;
    bool removeSyncFilter();
    bool clearNextBatch();
    bool checkAndSaveMegolmMessageIndex(const QString &sessionId, quint32 messageIndex,
                                        const QString &eventId, bool &replayDetected);
    void close();
    void loadRooms(const QString &profileDirectory, const QString &serverUrl,
                   const QString &userId);
    void loadHistory(const QString &profileDirectory, const QString &serverUrl,
                     const QString &userId, const QString &roomId);
    void checkCompleteCrossSigningKeys(const QString &userId);
    void loadHistoryPage(const QString &profileDirectory, const QString &serverUrl,
                         const QString &userId, const QString &roomId, int limit,
                         qint64 beforeOriginTs = 0, const QString &beforeEventId = QString());

signals:
    void roomsLoaded(const QList<MatrixCachedRoom> &rooms);
    void loadFailed(const QString &error);
    void historyLoaded(const QString &roomId, const QList<MatrixTimelineEvent> &events);
    void historyLoadFailed(const QString &roomId, const QString &error);
    void completeCrossSigningKeysChecked(const QString &userId, bool complete);
    void historyPageLoaded(const QString &roomId, const MatrixHistoryPageResult &result);

private:
    MatrixDatabase FDatabase;
    bool loadHistoryPageForAccount(const QString &profileDirectory,
                                   const QString &serverUrl,
                                   const QString &userId,
                                   const QString &roomId, int limit,
                                   qint64 beforeOriginTs,
                                   const QString &beforeEventId,
                                   MatrixHistoryPageResult &page);
};

#endif
