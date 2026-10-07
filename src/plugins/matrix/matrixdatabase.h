#ifndef MATRIXDATABASE_H
#define MATRIXDATABASE_H

#include <QString>
#include <QSqlDatabase>
#include <QDateTime>
#include <QMap>

// Timeline Event: protocol-neutral structure for room timeline events
struct MatrixTimelineEvent {
    QString roomId;       // m.room.id (e.g. "!xyz:matrix.org")
    QString eventId;      // Event identifier
    QString eventType;    // Event type (m.room.message, m.sticker, etc.)
    QString sender;       // Matrix user ID (sender)
    qint64 originTs;      // Event timestamp (ms since epoch)
    QString messageType;  // Message subtype (m.text, m.emote, m.image, etc.)
    QString content;      // Main text body or fallback content
    QStringList attachments; // URLs or filenames
    QHash<QString, QVariant> metadata; // Additional event metadata

    MatrixTimelineEvent() : originTs(0) {}
    bool isValid() const {
        return !roomId.isEmpty() && !eventId.isEmpty() && originTs > 0;
    }
};

struct MatrixOutboxEntry
{
    QString roomId;
    QString transactionId;
    QString body;
    QString status;
};

struct MatrixStoredRoom
{
    QString roomId;
    QString name;
    QString topic;
    QString avatarUrl;
    QString roomType;
    QString membership;
    bool isDirect = false;
    bool isEncrypted = false;
    bool encryptionStateKnown = false;
};

struct MatrixStoredMember
{
    QString userId;
    QString membership;
    QString displayName;
    QString avatarUrl;
};

// Timeline query result with window controls
struct MatrixTimelineCursor {
    qint64 syncToken;                     // Sync token from Matrix Sync API
    qint64 prevBatchTs;                    // TS of prev_batch boundary in API
    QList<MatrixTimelineEvent> events;     // Loaded timeline events
    bool hasMore;                          // True if more events exist
    QString nextToken;                     // Sync token for continuation

    MatrixTimelineCursor() : syncToken(0), prevBatchTs(0), hasMore(false) {}
    bool isValid() const { return !events.isEmpty() || hasMore; }
};

// History page result for keyset pagination
struct MatrixHistoryPageResult {
    QList<MatrixTimelineEvent> events;
    bool hasMore = false;
    bool success = false;
};

Q_DECLARE_METATYPE(MatrixTimelineEvent)
Q_DECLARE_METATYPE(QList<MatrixTimelineEvent>)
Q_DECLARE_METATYPE(MatrixHistoryPageResult)

// Database singleton for Matrix account-per-room timeline persistence
class MatrixDatabase
{
public:
    MatrixDatabase();
    ~MatrixDatabase();

    // Singleton access
    static MatrixDatabase &instance();
    static void cleanup(); // Close database on shutdown

    // Account-specific database opening
    bool openForAccount(const QString &serverUrl, const QString &userId);
    void setProfileDirectory(const QString &profileDirectory);
    QString profileDirectory() const;
    bool updateRoomName(const QString &roomId, const QString &name);
    void close();

    // Timeline queries
    MatrixTimelineCursor loadTimeline(const QString &roomId, qint64 afterTs, int limit = 100);
    MatrixHistoryPageResult loadHistoryPage(const QString &roomId, int limit,
                                            qint64 beforeOriginTs = 0,
                                            const QString &beforeEventId = QString()) const;
    bool saveTimeline(const QString &roomId, const QList<MatrixTimelineEvent> &events);
    bool appendTimelineEvents(const QString &roomId, const QList<MatrixTimelineEvent> &events);
    bool persistHistoryBackfillPage(const QString &roomId,
                                    const QList<MatrixTimelineEvent> &events,
                                    const QString &nextPrevBatch, bool limited);
    bool persistSyncBatch(const QList<MatrixTimelineEvent> &events, const QString &nextBatch);
    bool beginSyncMetadataBatch();
    bool commitSyncMetadataBatch();
    void rollbackSyncMetadataBatch();
    bool replaceTimelineEventId(const QString &roomId, const QString &oldEventId,
                                const QString &newEventId);
    bool updateTimelineEvent(const MatrixTimelineEvent &event);
    bool removeTimelineEvent(const QString &roomId, const QString &eventId);
    
    // Event deduplication and ordering
    bool hasEvent(const QString &roomId, const QString &eventId) const;
    QList<MatrixTimelineEvent> getEvents(const QString &roomId, int limit = 0) const;
    QStringList roomIds() const;
    QList<MatrixStoredRoom> roomStates() const;
    QList<MatrixStoredMember> roomMembers(const QString &roomId) const;
    
    // Sync state
    bool setSyncToken(const QString &roomId, qint64 token);
    qint64 getSyncToken(const QString &roomId) const;
    bool setNextBatch(const QString &nextBatch);
    QString nextBatch() const;
    bool saveSyncFilter(const QString &filterId);
    QString syncFilter() const;
    bool removeSyncFilter();
    bool saveRoomState(const QString &roomId, const QString &name, const QString &topic,
                       const QString &avatarUrl, const QString &membership, bool isDirect,
                       bool isEncrypted, bool encryptionStateKnown,
                       const QString &replacementRoomId,
                       const QString &prevBatch);
    bool saveRoomEncryptionState(const QString &roomId, bool isEncrypted,
                                 bool encryptionStateKnown);
    bool saveRoomType(const QString &roomId, const QString &roomType,
                      const QString &roomVersion);
    bool saveRoomTimelineBoundary(const QString &roomId, const QString &prevBatch,
                                  bool limited);
    bool loadRoomTimelineBoundary(const QString &roomId, QString &prevBatch,
                                  bool &limited) const;
    bool saveRoomMember(const QString &roomId, const QString &userId, const QString &membership,
                        const QString &displayName, const QString &avatarUrl,
                        const QString &eventId);
    bool saveRoomMemberHistory(const QString &roomId, const QString &userId,
                               const QString &membership, const QString &displayName,
                               const QString &avatarUrl, qint64 validFrom,
                               const QString &eventId);
    QString historicalMemberDisplayName(const QString &roomId, const QString &userId,
                                        qint64 timestamp) const;
    bool saveDirectRooms(const QSet<QString> &roomIds);
    QSet<QString> directRooms() const;
    bool setReadMarkers(const QString &roomId, const QString &fullyReadEventId, const QString &readEventId);
    bool saveReceipt(const QString &roomId, const QString &userId, const QString &eventId,
                     const QString &receiptType, qint64 timestamp);
    bool saveRoomAccountData(const QString &roomId, const QString &eventType,
                             const QByteArray &contentJson);
    bool saveOutboxMessage(const QString &roomId, const QString &transactionId,
                           const QString &body, const QString &status);
    bool removeOutboxMessage(const QString &transactionId);
    QList<MatrixOutboxEntry> failedOutboxMessages() const;
    bool saveSentNotification(const QString &roomId, const QString &eventId);
    bool hasSentNotification(const QString &eventId) const;
    bool removeSentNotifications(const QString &roomId);
    bool saveOlmAccount(const QString &userId, const QString &deviceId,
                       const QByteArray &pickleKey, const QByteArray &pickle);
    bool hasOlmAccount(const QString &userId, const QString &deviceId) const;
    bool loadOlmAccount(const QString &userId, const QString &deviceId,
                       QByteArray &pickleKey, QByteArray &pickle) const;
    bool saveMegolmSession(const QString &sessionId, const QString &roomId,
                           const QString &senderKey, const QByteArray &pickle,
                           const QString &claimedEd25519 = QString(),
                           const QString &forwardingChainJson = QString());
    bool loadMegolmSession(const QString &sessionId, QString &roomId,
                           QString &senderKey, QByteArray &pickle,
                           QString *claimedEd25519 = nullptr,
                           QString *forwardingChainJson = nullptr) const;
    bool removeMegolmSession(const QString &sessionId);
    bool saveOutboundMegolmSession(const QString &roomId, const QString &sessionId,
                                   const QByteArray &pickle, qint64 createdAt = 0);
    bool loadOutboundMegolmSession(const QString &roomId, QString &sessionId,
                                   QByteArray &pickle) const;
    qint64 outboundMegolmSessionCreatedAt(const QString &roomId) const;
    bool saveMegolmKeyShare(const QString &sessionId, const QString &userId,
                            const QString &deviceId, quint32 minimumIndex = 0);
    bool wasMegolmKeyShared(const QString &sessionId, const QString &userId,
                            const QString &deviceId) const;
    int megolmKeyShareIndex(const QString &sessionId, const QString &userId,
                            const QString &deviceId) const;
    QList<QPair<QString, QString>> megolmKeyShareDevices(const QString &sessionId) const;
    bool saveKeyRequest(const QString &requestId, const QString &sender,
                        const QString &deviceId, const QString &roomId,
                        const QString &sessionId);
    bool hasKeyRequest(const QString &requestId) const;
    bool saveDeviceKeys(const QString &userId, const QString &deviceId,
                        const QByteArray &keysJson);
    bool removeDeviceKeysExcept(const QString &userId, const QStringList &deviceIds);
    bool removeDeviceKeysForUser(const QString &userId);
    bool loadDeviceKeys(const QString &userId, const QString &deviceId,
                        QByteArray &keysJson) const;
    QMap<QString, QByteArray> loadDeviceKeysForUser(const QString &userId) const;
    bool deviceKeyChanged(const QString &userId, const QString &deviceId) const;
    bool isDeviceVerified(const QString &userId, const QString &deviceId) const;
    bool userVerificationStates(const QStringList &userIds, QMap<QString, bool> &states) const;
    bool saveCrossSigningKey(const QString &userId, const QString &keyType,
                             const QByteArray &keyJson);
    bool removeCrossSigningKey(const QString &userId, const QString &keyType);
    bool loadCrossSigningKey(const QString &userId, const QString &keyType,
                             QByteArray &keyJson) const;
    bool hasCompleteCrossSigningKeys(const QString &userId) const;
    bool saveVerificationState(const QString &transactionId, const QString &userId,
                               const QString &deviceId, const QString &state,
                               const QByteArray &eventJson);
    bool saveDeviceTrust(const QString &userId, const QString &deviceId,
                         const QString &identityKey, const QString &trustState);
    bool saveOlmSession(const QString &userId, const QString &deviceId,
                        const QString &sessionId, const QByteArray &pickle);
    bool loadOlmSession(const QString &userId, const QString &deviceId,
                        QString &sessionId, QByteArray &pickle) const;
    bool clearOlmSessionsForUser(const QString &userId);
    bool removeOlmSessionsForDevice(const QString &userId, const QString &deviceId);
    bool ensureOlmSessionContext(const QString &userId, const QString &deviceId, bool &changed);
    bool removeOlmSession(const QString &userId, const QString &deviceId, const QString &sessionId);
    QList<QPair<QString, QByteArray>> loadOlmSessions(const QString &userId,
                                                       const QString &deviceId) const;
    bool checkAndSaveMegolmMessageIndex(const QString &sessionId, quint32 messageIndex,
                                        const QString &eventId, bool &replayDetected);
    
    // Message history helpers
    bool saveRawContent(const QString &roomId, const QString &eventId, 
                        const QString &serverUrl, const QString &userId);
    bool clearOldEvents(const QString &roomId, int maxAgeDays = 7);

private:
    // Database management
    void createTables();
    QString databasePath(const QString &userId) const;
    
    // Internal helpers
    bool createConnection();
    bool insertEvent(const MatrixTimelineEvent &event);
    bool updateMetadata(const QString &eventId, const QHash<QString, QVariant> &metadata);
    bool updateSyncTokenOnEvent(const QString &roomId, const QString &eventId, qint64 ts);
    
    QSqlDatabase FDatabase;
    QString FServerUrl;
    QString FUserId;
    QString FProfileDirectory;
    bool FisOpen;
    bool FSyncMetadataTransactionActive = false;
};

#endif // MATRIXDATABASE_H