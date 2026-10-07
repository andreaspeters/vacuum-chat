#include "matrixdatabase.h"
#include <QThread>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QStandardPaths>
#include <QDir>

#include <QDebug>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

MatrixDatabase &MatrixDatabase::instance()
{
    static MatrixDatabase db;
    return db;
}

void MatrixDatabase::cleanup()
{
    if (instance().FisOpen) {
        instance().close();
    }
}

MatrixDatabase::MatrixDatabase() : FisOpen(false)
{
}

MatrixDatabase::~MatrixDatabase()
{
    if (FisOpen) {
        close();
    }
}

bool MatrixDatabase::openForAccount(const QString &serverUrl, const QString &userId)
{
    const bool accountChanged = FisOpen &&
        (FServerUrl != serverUrl || FUserId != userId);
    if (accountChanged) {
        const QString connectionName = FDatabase.connectionName();
        FDatabase.close();
        FDatabase = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
        FisOpen = false;
    }
    FServerUrl = serverUrl;
    FUserId = userId;
    
    if (!createConnection()) {
        qWarning() << "Failed to open Matrix database for" << FUserId;
        return false;
    }
    
    createTables();
    FisOpen = true;
    return true;
}

void MatrixDatabase::setProfileDirectory(const QString &profileDirectory)
{
    if (FisOpen && FProfileDirectory != profileDirectory) {
        const QString connectionName = FDatabase.connectionName();
        FDatabase.close();
        FDatabase = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
        FisOpen = false;
    }
    FProfileDirectory = profileDirectory;
}

QString MatrixDatabase::profileDirectory() const
{
    return FProfileDirectory;
}

bool MatrixDatabase::updateRoomName(const QString &roomId, const QString &name)
{
    if (roomId.isEmpty())
        return false;
    QSqlQuery query(FDatabase);
    query.prepare(QStringLiteral("UPDATE rooms SET name = :name WHERE room_id = :room_id"));
    query.bindValue(QStringLiteral(":name"), name);
    query.bindValue(QStringLiteral(":room_id"), roomId);
    return query.exec();
}

void MatrixDatabase::close()
{
    if (FisOpen && FDatabase.isOpen()) {
        FDatabase.close();
    }
    FisOpen = false;
}

QString MatrixDatabase::databasePath(const QString &userId) const
{
    Q_UNUSED(userId);
    const QString directory = FProfileDirectory.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/matrix-db"
        : FProfileDirectory;
    QDir().mkpath(directory);

    return directory + QStringLiteral("/matrix.db");
}

bool MatrixDatabase::createConnection()
{
    if (FDatabase.isOpen()) {
        return true;
    }
    
    const QString path = databasePath(FUserId);
    const QString connectionName = QStringLiteral("matrix_%1_%2")
        .arg(FUserId, QString::number(quintptr(QThread::currentThreadId())));
    FDatabase = QSqlDatabase::addDatabase("QSQLITE", connectionName);
    FDatabase.setDatabaseName(path);
    
    if (!FDatabase.open()) {
        qCritical() << "Could not open Matrix database:" << FDatabase.lastError().text();
        return false;
    }
	QSqlQuery pragma(FDatabase);
	if (!pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL")))
		qWarning() << "Could not enable Matrix SQLite WAL:" << pragma.lastError().text();
	if (!pragma.exec(QStringLiteral("PRAGMA synchronous=NORMAL")))
		qWarning() << "Could not set Matrix SQLite synchronous mode:" << pragma.lastError().text();

    return true;
}

void MatrixDatabase::createTables()
{
    QSqlQuery query(FDatabase);
    
    // Timeline events table
    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS timeline_events ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "room_id TEXT NOT NULL,"
        "event_id TEXT NOT NULL,"
        "event_type TEXT,"
        "sender TEXT,"
        "origin_ts INTEGER NOT NULL,"
        "message_type TEXT,"
        "content TEXT,"
        "metadata TEXT,"
        "sync_token INTEGER DEFAULT 0,"
        "UNIQUE(room_id, event_id)"
        ")")) {
        qWarning() << "Failed to create timeline_events table:" << query.lastError();
    }
    if (!query.exec("CREATE INDEX IF NOT EXISTS idx_timeline_events_room_time "
                   "ON timeline_events(room_id, origin_ts, event_id)")) {
        qWarning() << "Failed to create timeline_events room/time index:" << query.lastError();
    }
    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS timeline_relations ("
        "source_event_id TEXT PRIMARY KEY,"
        "room_id TEXT NOT NULL,"
        "target_event_id TEXT NOT NULL,"
        "relation_type TEXT NOT NULL,"
        "relation_json TEXT"
        ")")) {
        qWarning() << "Failed to create timeline_relations table:" << query.lastError();
    }
    
    // Sync state table (per-room)
    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS sync_state ("
        "room_id TEXT PRIMARY KEY,"
        "sync_token INTEGER NOT NULL,"
        "prev_batch_ts INTEGER DEFAULT 0"
        ")")) {
        qWarning() << "Failed to create sync_state table:" << query.lastError();
    }
    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS sync_filter ("
        "id INTEGER PRIMARY KEY CHECK (id = 1), filter_id TEXT NOT NULL"
        ")")) {
        qWarning() << "Failed to create sync_filter table:" << query.lastError();
    }
    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS sync_cursor ("
        "id INTEGER PRIMARY KEY CHECK (id = 1),"
        "next_batch TEXT NOT NULL"
        ")")) {
        qWarning() << "Failed to create sync_cursor table:" << query.lastError();
    }
    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS rooms ("
        "room_id TEXT PRIMARY KEY,"
        "room_type TEXT, room_version TEXT,"
        "name TEXT, topic TEXT, avatar_url TEXT, membership TEXT,"
        "is_direct INTEGER NOT NULL DEFAULT 0,"
        "is_encrypted INTEGER NOT NULL DEFAULT 0,"
        "replacement_room_id TEXT, prev_batch TEXT, limited INTEGER NOT NULL DEFAULT 0"
        ")")) {
        qWarning() << "Failed to create rooms table:" << query.lastError();
    }
    bool hasLimitedColumn = false;
    if (query.exec(QStringLiteral("PRAGMA table_info(rooms)"))) {
        while (query.next()) {
            if (query.value(1).toString() == QStringLiteral("limited")) {
                hasLimitedColumn = true;
                break;
            }
        }
    }
    if (!hasLimitedColumn && !query.exec(QStringLiteral(
        "ALTER TABLE rooms ADD COLUMN limited INTEGER NOT NULL DEFAULT 0")))
        qWarning() << "Failed to migrate rooms limited column:" << query.lastError();
    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS contacts ("
        "user_id TEXT PRIMARY KEY,"
        "display_name TEXT,"
        "avatar_url TEXT"
        ")")) {
        qWarning() << "Failed to create contacts table:" << query.lastError();
    }
    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS room_members ("
        "room_id TEXT NOT NULL,"
        "user_id TEXT NOT NULL,"
        "membership TEXT NOT NULL,"
        "display_name TEXT,"
        "avatar_url TEXT,"
        "event_id TEXT,"
        "PRIMARY KEY (room_id, user_id)"
        ")")) {
        qWarning() << "Failed to create room_members table:" << query.lastError();
    }
    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS room_member_history ("
        "room_id TEXT NOT NULL, user_id TEXT NOT NULL, membership TEXT NOT NULL,"
        "display_name TEXT, avatar_url TEXT, valid_from INTEGER NOT NULL, event_id TEXT,"
        "PRIMARY KEY (room_id, user_id, valid_from)"
        ")")) {
        qWarning() << "Failed to create room_member_history table:" << query.lastError();
    }
    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS messages ("
        "event_id TEXT PRIMARY KEY,"
        "room_id TEXT NOT NULL,"
        "sender TEXT NOT NULL,"
        "msgtype TEXT NOT NULL,"
        "body TEXT,"
        "timestamp INTEGER NOT NULL,"
        "transaction_id TEXT,"
        "send_state TEXT NOT NULL DEFAULT 'sent'"
        ")")) {
        qWarning() << "Failed to create messages table:" << query.lastError();
    }
    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS direct_rooms ("
        "room_id TEXT PRIMARY KEY"
        ")")) {
        qWarning() << "Failed to create direct_rooms table:" << query.lastError();
    }

    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS room_state ("
        "room_id TEXT PRIMARY KEY,"
        "fully_read_event_id TEXT,"
        "read_event_id TEXT"
        ")")) {
        qWarning() << "Failed to create room_state table:" << query.lastError();
    }

    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS room_receipts ("
        "room_id TEXT NOT NULL, user_id TEXT NOT NULL, event_id TEXT NOT NULL,"
        "receipt_type TEXT NOT NULL, timestamp INTEGER NOT NULL,"
        "PRIMARY KEY (room_id, user_id, receipt_type)"
        ")")) {
        qWarning() << "Failed to create room_receipts table:" << query.lastError();
    }
    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS room_account_data ("
        "room_id TEXT NOT NULL, event_type TEXT NOT NULL, content_json TEXT NOT NULL,"
        "PRIMARY KEY (room_id, event_type)"
        ")")) {
        qWarning() << "Failed to create room_account_data table:" << query.lastError();
    }

    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS outbox ("
        "transaction_id TEXT PRIMARY KEY,"
        "room_id TEXT NOT NULL,"
        "body TEXT NOT NULL,"
        "status TEXT NOT NULL"
        ")")) {
        qWarning() << "Failed to create outbox table:" << query.lastError();
    }

    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS sent_notifications ("
        "event_id TEXT PRIMARY KEY,"
        "room_id TEXT NOT NULL"
        ")")) {
        qWarning() << "Failed to create sent_notifications table:" << query.lastError();
    }

    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS olm_accounts ("
        "user_id TEXT NOT NULL,"
        "device_id TEXT NOT NULL,"
        "pickle_key BLOB NOT NULL,"
        "pickle BLOB NOT NULL,"
        "PRIMARY KEY (user_id, device_id)"
        ")")) {
        qWarning() << "Failed to create olm_accounts table:" << query.lastError();
    }

    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS olm_megolm_sessions ("
        "session_id TEXT PRIMARY KEY,"
        "room_id TEXT NOT NULL,"
        "sender_key TEXT NOT NULL,"
        "pickle BLOB NOT NULL,"
        "sender_claimed_ed25519 TEXT NOT NULL DEFAULT '',"
        "forwarding_curve25519_key_chain TEXT NOT NULL DEFAULT '[]'"
        ")")) {
        qWarning() << "Failed to create olm_megolm_sessions table:" << query.lastError();
    }
    bool hasClaimedEd25519 = false;
    bool hasForwardingChain = false;
    if (query.exec("PRAGMA table_info(olm_megolm_sessions)")) {
        while (query.next()) {
            const QString column = query.value(1).toString();
            hasClaimedEd25519 = hasClaimedEd25519 || column == QStringLiteral("sender_claimed_ed25519");
            hasForwardingChain = hasForwardingChain || column == QStringLiteral("forwarding_curve25519_key_chain");
        }
    }
    if (!hasClaimedEd25519 && !query.exec(
        "ALTER TABLE olm_megolm_sessions ADD COLUMN sender_claimed_ed25519 TEXT NOT NULL DEFAULT ''"))
        qWarning() << "Failed to migrate Megolm claimed Ed25519:" << query.lastError();
    if (!hasForwardingChain && !query.exec(
        "ALTER TABLE olm_megolm_sessions ADD COLUMN forwarding_curve25519_key_chain TEXT NOT NULL DEFAULT '[]'"))
        qWarning() << "Failed to migrate Megolm forwarding chain:" << query.lastError();
    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS olm_outbound_megolm_sessions ("
        "room_id TEXT PRIMARY KEY, session_id TEXT NOT NULL, pickle BLOB NOT NULL, "
        "created_at INTEGER NOT NULL DEFAULT 0"
        ")")) {
        qWarning() << "Failed to create olm_outbound_megolm_sessions table:" << query.lastError();
    }
    bool hasOutboundCreatedAt = false;
    if (query.exec("PRAGMA table_info(olm_outbound_megolm_sessions)")) {
        while (query.next()) {
            if (query.value(1).toString() == QStringLiteral("created_at")) {
                hasOutboundCreatedAt = true;
                break;
            }
        }
    }
    if (!hasOutboundCreatedAt && !query.exec(
        "ALTER TABLE olm_outbound_megolm_sessions ADD COLUMN created_at INTEGER NOT NULL DEFAULT 0"))
        qWarning() << "Failed to migrate outbound Megolm created_at:" << query.lastError();

    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS olm_key_requests ("
        "request_id TEXT PRIMARY KEY,"
        "sender TEXT NOT NULL,"
        "device_id TEXT NOT NULL,"
        "room_id TEXT NOT NULL,"
        "session_id TEXT NOT NULL"
        ")")) {
        qWarning() << "Failed to create olm_key_requests table:" << query.lastError();
    }
    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS olm_megolm_key_shares ("
        "session_id TEXT NOT NULL, user_id TEXT NOT NULL, device_id TEXT NOT NULL, "
        "minimum_index INTEGER NOT NULL DEFAULT 0, "
        "PRIMARY KEY (session_id, user_id, device_id)"
        ")")) {
        qWarning() << "Failed to create olm_megolm_key_shares table:" << query.lastError();
    }

    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS olm_device_keys ("
        "user_id TEXT NOT NULL,"
        "device_id TEXT NOT NULL,"
        "keys_json BLOB NOT NULL,"
        "PRIMARY KEY (user_id, device_id)"
        ")")) {
        qWarning() << "Failed to create olm_device_keys table:" << query.lastError();
    }
    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS device_trust ("
        "user_id TEXT NOT NULL, device_id TEXT NOT NULL, identity_key TEXT NOT NULL,"
        "trust_state TEXT NOT NULL DEFAULT 'unverified', key_changed INTEGER NOT NULL DEFAULT 0,"
        "PRIMARY KEY (user_id, device_id)"
        ")")) {
        qWarning() << "Failed to create device_trust table:" << query.lastError();
    }
    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS cross_signing_keys ("
        "user_id TEXT NOT NULL, key_type TEXT NOT NULL, key_json BLOB NOT NULL, "
        "updated_at INTEGER NOT NULL, PRIMARY KEY (user_id, key_type)"
        ")")) {
        qWarning() << "Failed to create cross_signing_keys table:" << query.lastError();
    }
    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS sas_verifications ("
        "transaction_id TEXT PRIMARY KEY, user_id TEXT NOT NULL, device_id TEXT NOT NULL,"
        "state TEXT NOT NULL, event_json BLOB NOT NULL, updated_at INTEGER NOT NULL"
        ")")) {
        qWarning() << "Failed to create sas_verifications table:" << query.lastError();
    }

    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS olm_sessions ("
        "user_id TEXT NOT NULL,"
        "device_id TEXT NOT NULL,"
        "session_id TEXT NOT NULL,"
        "pickle BLOB NOT NULL,"
        "PRIMARY KEY (user_id, device_id, session_id)"
        ")")) {
        qWarning() << "Failed to create olm_sessions table:" << query.lastError();
    }
    bool hasSessionId = false;
    if (query.exec("PRAGMA table_info(olm_sessions)")) {
        while (query.next()) {
            if (query.value(1).toString() == QStringLiteral("session_id")) {
                hasSessionId = true;
                break;
            }
        }
    }
    if (!hasSessionId && !query.exec(
        "ALTER TABLE olm_sessions ADD COLUMN session_id TEXT NOT NULL DEFAULT ''")) {
        qWarning() << "Failed to migrate olm_sessions session_id:" << query.lastError();
    }
    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS olm_sessions_v2 ("
        "user_id TEXT NOT NULL, device_id TEXT NOT NULL, session_id TEXT NOT NULL, "
        "pickle BLOB NOT NULL, PRIMARY KEY (user_id, device_id, session_id)"
        ")")) {
        qWarning() << "Failed to create olm_sessions_v2 table:" << query.lastError();
    }
    if (!query.exec(
        "INSERT OR IGNORE INTO olm_sessions_v2 (user_id, device_id, session_id, pickle) "
        "SELECT user_id, device_id, session_id, pickle FROM olm_sessions "
        "WHERE session_id != ''")) {
        qWarning() << "Failed to migrate olm sessions to v2:" << query.lastError();
    }
    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS olm_megolm_message_indices ("
        "session_id TEXT NOT NULL,"
        "message_index INTEGER NOT NULL,"
        "event_id TEXT NOT NULL,"
        "PRIMARY KEY (session_id, message_index)"
        ")")) {
        qWarning() << "Failed to create olm_megolm_message_indices table:" << query.lastError();
    }
}

MatrixTimelineCursor MatrixDatabase::loadTimeline(const QString &roomId, qint64 afterTs, int limit)
{
    MatrixTimelineCursor cursor;
    
    QSqlQuery query(FDatabase);
    QString sql = "SELECT room_id, event_id, event_type, sender, origin_ts, "
                  "message_type, content, metadata FROM timeline_events "
                  "WHERE room_id = :room_id";
    
    // Apply offset based on afterTs (descending by origin_ts)
    if (afterTs > 0) {
        sql += " AND origin_ts < :after_ts ORDER BY origin_ts DESC, event_id DESC LIMIT :limit";
    } else {
        sql += " ORDER BY origin_ts DESC, event_id DESC LIMIT :limit";
    }
    
    if (!query.prepare(sql)) {
        qWarning() << "Failed to prepare timeline query:" << query.lastError();
        return cursor;
    }
    
    query.bindValue(":room_id", roomId);
    query.bindValue(":after_ts", afterTs);
    query.bindValue(":limit", limit);
    
    if (!query.exec()) {
        qWarning() << "Failed to execute timeline query:" << query.lastError();
        return cursor;
    }
    
    // Load events
    while (query.next()) {
        QSqlRecord rec = query.record();
        MatrixTimelineEvent event;
        
        event.roomId = rec.value("room_id").toString();
        event.eventId = rec.value("event_id").toString();
        event.eventType = rec.value("event_type").toString();
        event.sender = rec.value("sender").toString();
        event.originTs = rec.value("origin_ts").toLongLong();
        event.messageType = rec.value("message_type").toString();
        event.content = rec.value("content").toString();
        
        // Parse metadata JSON
        const QByteArray metaJson = rec.value("metadata").toByteArray();
        if (!metaJson.isEmpty()) {
            QJsonDocument doc = QJsonDocument::fromJson(metaJson);
            if (doc.isObject()) {
                QJsonObject metaObj = doc.object();
                for (auto it = metaObj.begin(); it != metaObj.end(); ++it) {
                    event.metadata.insert(it.key(), it.value().toVariant());
                }
            }
        }
        
        cursor.events.append(event);
        
        // Track latest sync token from loaded events (max)
        qint64 syncToken = rec.value("sync_token").toLongLong();
        if (syncToken > cursor.syncToken) {
            cursor.syncToken = syncToken;
            cursor.nextToken = QString::number(syncToken); // Approximation
        }
    }
    
    // Check for more events after the loaded window
    qint64 oldestTs = 0;
    if (!cursor.events.isEmpty()) {
        // Inverse order in SQL, so oldest is last
        const auto &oldest = cursor.events.last();
        oldestTs = oldest.originTs;
    }
    
    // Detect more: count all before oldest + limit window size
    if (oldestTs > 0) {
        QSqlQuery countQuery(FDatabase);
        if (countQuery.exec(
                QString("SELECT COUNT(*) FROM timeline_events WHERE room_id = :room_id "
                       "AND origin_ts < :after_ts OR (origin_ts = :after_ts AND "
                       "event_id < :last_event_id ORDER BY event_id DESC LIMIT 1)"))) {
            countQuery.bindValue(":room_id", roomId);
            countQuery.bindValue(":after_ts", oldestTs);
            countQuery.bindValue(":last_event_id", cursor.events.last().eventId);
            
            if (countQuery.next()) {
                int total = countQuery.value(0).toInt();
                cursor.hasMore = total > cursor.events.size();
            }
        }
    } else {
        // No events loaded yet, more exists if total > 0
        QSqlQuery countQuery(FDatabase);
        if (countQuery.exec(
                QString("SELECT COUNT(*) FROM timeline_events WHERE room_id = :room_id"))) {
            countQuery.bindValue(":room_id", roomId);
            
            if (countQuery.next()) {
                cursor.hasMore = countQuery.value(0).toInt() > 0;
            }
        }
    }
    
    return cursor;
}

bool MatrixDatabase::saveTimeline(const QString &roomId, 
                                   const QList<MatrixTimelineEvent> &events)
{
    if (!FDatabase.transaction()) {
        qWarning() << "Failed to begin transaction:" << FDatabase.lastError();
        return false;
    }
    
    bool success = true;
    
    for (const auto &event : events) {
        if (!event.isValid()) {
            qWarning() << "Skipped invalid event in timeline save:" << event.eventId;
            continue;
        }
        
        if (!insertEvent(event)) {
            success = false;
            break;
        }
    }
    
    if (success) {
        if (!FDatabase.commit()) {
            qWarning() << "Failed to commit transaction:" << FDatabase.lastError();
            success = false;
        }
    } else {
        FDatabase.rollback();
    }
    
    return success;
}

bool MatrixDatabase::appendTimelineEvents(const QString &roomId,
                                           const QList<MatrixTimelineEvent> &events)
{
    QSqlQuery query(FDatabase);
    
    // Check existing events to filter duplicates
    QStringList existingIds;
    for (const auto &event : events) {
        if (hasEvent(roomId, event.eventId)) {
            existingIds.append(event.eventId);
        }
    }
    
    if (!FDatabase.transaction()) {
        qWarning() << "Failed to begin append transaction:" << FDatabase.lastError();
        return false;
    }
    
    bool success = true;
    
    for (const auto &event : events) {
        if (existingIds.contains(event.eventId)) {
            continue;
        }
        
        if (!event.isValid()) {
            continue;
        }
        
        if (!insertEvent(event)) {
            success = false;
            break;
        }
        
        // Update sync token to latest event
        if (event.originTs > 0) {
            updateSyncTokenOnEvent(roomId, event.eventId, event.originTs);
        }
    }
    
    if (success) {
        if (!FDatabase.commit()) {
            qWarning() << "Failed to commit append transaction:" << FDatabase.lastError();
            success = false;
        }
    } else {
        FDatabase.rollback();
    }
    
    return success;
}

bool MatrixDatabase::persistSyncBatch(const QList<MatrixTimelineEvent> &events,
                                      const QString &nextBatch)
{
    if (nextBatch.isEmpty())
        return false;
    if (!FDatabase.transaction()) {
        qWarning() << "Failed to begin sync batch transaction:" << FDatabase.lastError();
        return false;
    }

    bool success = true;
    for (const MatrixTimelineEvent &event : events) {
        if (!event.isValid() || !insertEvent(event)) {
            success = false;
            break;
        }
    }
    if (success) {
        QSqlQuery query(FDatabase);
        query.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO sync_cursor (id, next_batch) VALUES (1, :next_batch)"));
        query.bindValue(QStringLiteral(":next_batch"), nextBatch);
        success = query.exec();
    }
    if (success && !FDatabase.commit()) {
        qWarning() << "Failed to commit sync batch transaction:" << FDatabase.lastError();
        success = false;
    }
    if (!success)
        FDatabase.rollback();
    return success;
}

bool MatrixDatabase::beginSyncMetadataBatch()
{
    FSyncMetadataTransactionActive = FDatabase.transaction();
    return FSyncMetadataTransactionActive;
}

bool MatrixDatabase::commitSyncMetadataBatch()
{
    const bool success = FDatabase.commit();
    if (success)
        FSyncMetadataTransactionActive = false;
    return success;
}

void MatrixDatabase::rollbackSyncMetadataBatch()
{
    FDatabase.rollback();
    FSyncMetadataTransactionActive = false;
}

bool MatrixDatabase::replaceTimelineEventId(const QString &roomId, const QString &oldEventId,
                                            const QString &newEventId)
{
    if (roomId.isEmpty() || oldEventId.isEmpty() || newEventId.isEmpty())
        return false;
    QSqlQuery query(FDatabase);
    query.prepare("UPDATE timeline_events SET event_id = :new_id "
                  "WHERE room_id = :room_id AND event_id = :old_id");
    query.bindValue(":new_id", newEventId);
    query.bindValue(":room_id", roomId);
    query.bindValue(":old_id", oldEventId);
    return query.exec();
}

bool MatrixDatabase::removeTimelineEvent(const QString &roomId, const QString &eventId)
{
    if (roomId.isEmpty() || eventId.isEmpty())
        return false;
    const bool ownTransaction = !FSyncMetadataTransactionActive;
    if (ownTransaction && !FDatabase.transaction())
        return false;
    bool success = true;
    QSqlQuery relations(FDatabase);
    relations.prepare(QStringLiteral(
        "DELETE FROM timeline_relations WHERE source_event_id = :event_id "
        "OR target_event_id = :event_id"));
    relations.bindValue(QStringLiteral(":event_id"), eventId);
    success = relations.exec();
    if (success) {
        QSqlQuery message(FDatabase);
        message.prepare(QStringLiteral("DELETE FROM messages "
            "WHERE room_id = :room_id AND event_id = :event_id"));
        message.bindValue(QStringLiteral(":room_id"), roomId);
        message.bindValue(QStringLiteral(":event_id"), eventId);
        success = message.exec();
    }
    if (success) {
        QSqlQuery timeline(FDatabase);
        timeline.prepare(QStringLiteral(
            "DELETE FROM timeline_events WHERE room_id = :room_id AND event_id = :event_id"));
        timeline.bindValue(QStringLiteral(":room_id"), roomId);
        timeline.bindValue(QStringLiteral(":event_id"), eventId);
        success = timeline.exec();
    }
    if (ownTransaction) {
        if (success)
            success = FDatabase.commit();
        else
            FDatabase.rollback();
    }
    return success;
}
bool MatrixDatabase::updateTimelineEvent(const MatrixTimelineEvent &event)
{
    if (!event.isValid())
        return false;
    QJsonObject metaObj;
    for (auto it = event.metadata.constBegin(); it != event.metadata.constEnd(); ++it)
        metaObj.insert(it.key(), QJsonValue::fromVariant(it.value()));
    QSqlQuery query(FDatabase);
    query.prepare(QStringLiteral(
        "UPDATE timeline_events SET event_type = :event_type, sender = :sender, "
        "origin_ts = :origin_ts, message_type = :message_type, content = :content, "
        "metadata = :metadata WHERE room_id = :room_id AND event_id = :event_id"));
    query.bindValue(QStringLiteral(":event_type"), event.eventType);
    query.bindValue(QStringLiteral(":sender"), event.sender);
    query.bindValue(QStringLiteral(":origin_ts"), event.originTs);
    query.bindValue(QStringLiteral(":message_type"), event.messageType);
    query.bindValue(QStringLiteral(":content"), event.content);
    query.bindValue(QStringLiteral(":metadata"), QString::fromUtf8(
        QJsonDocument(metaObj).toJson(QJsonDocument::Compact)));
    query.bindValue(QStringLiteral(":room_id"), event.roomId);
    query.bindValue(QStringLiteral(":event_id"), event.eventId);
    return query.exec();
}

bool MatrixDatabase::insertEvent(const MatrixTimelineEvent &event)
{
    QSqlQuery query(FDatabase);
    
    // Serialize metadata JSON
    QJsonObject metaObj;
    for (auto it = event.metadata.begin(); it != event.metadata.end(); ++it) {
        metaObj.insert(it.key(), QJsonValue::fromVariant(it.value()));
    }
    QByteArray metaJson = QJsonDocument(metaObj).toJson(QJsonDocument::Compact);
    
    query.prepare(
        "INSERT OR IGNORE INTO timeline_events "
        "(room_id, event_id, event_type, sender, origin_ts, message_type, "
        "content, metadata) "
        "VALUES (:room_id, :event_id, :event_type, :sender, :origin_ts, "
                ":message_type, :content, :metadata)"
    );
    
    query.bindValue(":room_id", event.roomId);
    query.bindValue(":event_id", event.eventId);
    query.bindValue(":event_type", event.eventType);
    query.bindValue(":sender", event.sender);
    query.bindValue(":origin_ts", event.originTs);
    query.bindValue(":message_type", event.messageType);
    query.bindValue(":content", event.content);
    query.bindValue(":metadata", QString::fromUtf8(metaJson));
    
    if (!query.exec()) {
        qWarning() << "Failed to insert event:" << query.lastError();
        return false;
    }
	if (event.eventType == QStringLiteral("m.room.message")) {
		QSqlQuery messageQuery(FDatabase);
		const QString materializedMessageType = event.messageType.isEmpty()
			? QStringLiteral("m.unknown") : event.messageType;
		messageQuery.prepare(QStringLiteral(
			"INSERT OR REPLACE INTO messages "
			"(event_id, room_id, sender, msgtype, body, timestamp, transaction_id, send_state) "
			"VALUES (:event_id, :room_id, :sender, :msgtype, :body, :timestamp, :transaction_id, :send_state)"));
		messageQuery.bindValue(QStringLiteral(":event_id"), event.eventId);
		messageQuery.bindValue(QStringLiteral(":room_id"), event.roomId);
		messageQuery.bindValue(QStringLiteral(":sender"), event.sender);
		messageQuery.bindValue(QStringLiteral(":msgtype"), materializedMessageType);
		messageQuery.bindValue(QStringLiteral(":body"), event.content);
		messageQuery.bindValue(QStringLiteral(":timestamp"), event.originTs);
		messageQuery.bindValue(QStringLiteral(":transaction_id"),
			event.metadata.value(QStringLiteral("txn_id")));
		messageQuery.bindValue(QStringLiteral(":send_state"),
			event.metadata.value(QStringLiteral("send_state"), QStringLiteral("sent")));
		if (!messageQuery.exec()) {
			qWarning() << "Failed to materialize Matrix message:" << messageQuery.lastError()
				<< "eventType:" << event.eventType
				<< "materializedMsgType:" << materializedMessageType;
			return false;
		}
	}

    QString relationType = event.metadata.value(QStringLiteral("relation_type")).toString();
    QString targetEventId = event.metadata.value(QStringLiteral("related_event_id")).toString();
    if (relationType.isEmpty() && event.eventType == QStringLiteral("m.room.redaction")) {
        relationType = QStringLiteral("m.redaction");
        targetEventId = event.metadata.value(QStringLiteral("redacts")).toString();
    }
    if (!relationType.isEmpty() && !targetEventId.isEmpty()) {
        QSqlQuery relationQuery(FDatabase);
        relationQuery.prepare(
            "INSERT OR REPLACE INTO timeline_relations "
            "(source_event_id, room_id, target_event_id, relation_type, relation_json) "
            "VALUES (:source, :room, :target, :type, :json)");
        relationQuery.bindValue(":source", event.eventId);
        relationQuery.bindValue(":room", event.roomId);
        relationQuery.bindValue(":target", targetEventId);
        relationQuery.bindValue(":type", relationType);
        relationQuery.bindValue(":json", QString::fromUtf8(QJsonDocument(metaObj).toJson(QJsonDocument::Compact)));
        if (!relationQuery.exec())
            qWarning() << "Failed to insert timeline relation:" << relationQuery.lastError();

        QString dependencyKey;
        if (relationType == QStringLiteral("m.replace"))
            dependencyKey = QStringLiteral("edit_event_ids");
        else if (relationType == QStringLiteral("m.annotation"))
            dependencyKey = QStringLiteral("reaction_event_ids");
        else if (relationType == QStringLiteral("m.redaction"))
            dependencyKey = QStringLiteral("redaction_event_ids");
        if (!dependencyKey.isEmpty()) {
            QSqlQuery targetQuery(FDatabase);
            targetQuery.prepare("SELECT metadata FROM timeline_events WHERE room_id = :room "
                                "AND event_id = :event");
            targetQuery.bindValue(":room", event.roomId);
            targetQuery.bindValue(":event", targetEventId);
            if (targetQuery.exec() && targetQuery.next()) {
                QJsonObject targetMetadata = QJsonDocument::fromJson(
                    targetQuery.value(0).toByteArray()).object();
                QJsonArray dependencies = targetMetadata.value(dependencyKey).toArray();
                if (!dependencies.contains(event.eventId))
                    dependencies.append(event.eventId);
                targetMetadata.insert(dependencyKey, dependencies);
                QSqlQuery updateQuery(FDatabase);
                updateQuery.prepare("UPDATE timeline_events SET metadata = :metadata "
                                    "WHERE room_id = :room AND event_id = :event");
                updateQuery.bindValue(":metadata", QString::fromUtf8(
                    QJsonDocument(targetMetadata).toJson(QJsonDocument::Compact)));
                updateQuery.bindValue(":room", event.roomId);
                updateQuery.bindValue(":event", targetEventId);
                if (!updateQuery.exec())
                    qWarning() << "Failed to update timeline relation target:" << updateQuery.lastError();
            }
        }
    }
    
    return true;
}

bool MatrixDatabase::hasEvent(const QString &roomId, const QString &eventId) const
{
    QSqlQuery query(FDatabase);
    query.prepare("SELECT COUNT(*) FROM timeline_events WHERE room_id = :room_id AND event_id = :event_id");
    query.bindValue(":room_id", roomId);
    query.bindValue(":event_id", eventId);
    
    if (!query.exec()) {
        return false;
    }
    
    if (query.next()) {
        return query.value(0).toInt() > 0;
    }
    
    return false;
}

QList<MatrixTimelineEvent> MatrixDatabase::getEvents(const QString &roomId, int limit) const
{
    QList<MatrixTimelineEvent> events;
    
    QSqlQuery query(FDatabase);
    QString sql = "SELECT room_id, event_id, event_type, sender, origin_ts, "
                  "message_type, content, metadata FROM timeline_events "
                  "WHERE room_id = :room_id ORDER BY origin_ts ASC";
    
    if (limit > 0) {
        sql += " LIMIT :limit";
    }
    
    query.prepare(sql);
    query.bindValue(":room_id", roomId);
    if (limit > 0) {
        query.bindValue(":limit", limit);
    }
    
    if (!query.exec()) {
        qWarning() << "Failed to query events:" << query.lastError();
        return events;
    }
    
    while (query.next()) {
        QSqlRecord rec = query.record();
        MatrixTimelineEvent event;
        
        event.roomId = rec.value("room_id").toString();
        event.eventId = rec.value("event_id").toString();
        event.eventType = rec.value("event_type").toString();
        event.sender = rec.value("sender").toString();
        event.originTs = rec.value("origin_ts").toLongLong();
        event.messageType = rec.value("message_type").toString();
        event.content = rec.value("content").toString();
        
        // Parse metadata JSON
        const QByteArray metaJson = rec.value("metadata").toByteArray();
        if (!metaJson.isEmpty()) {
            QJsonDocument doc = QJsonDocument::fromJson(metaJson);
            if (doc.isObject()) {
                QJsonObject metaObj = doc.object();
                for (auto it = metaObj.begin(); it != metaObj.end(); ++it) {
                    event.metadata.insert(it.key(), it.value().toVariant());
                }
            }
        }
        
        events.append(event);
    }
    
    return events;
}

MatrixHistoryPageResult MatrixDatabase::loadHistoryPage(const QString &roomId, int limit,
                                                        qint64 beforeOriginTs,
                                                        const QString &beforeEventId) const
{
    MatrixHistoryPageResult result;
    if (!FisOpen || !FDatabase.isOpen() || roomId.isEmpty() || limit <= 0 ||
        beforeOriginTs < 0 ||
        (beforeOriginTs == 0 && !beforeEventId.isEmpty()) ||
        (beforeOriginTs > 0 && beforeEventId.isEmpty()))
        return result;

    // History requests are deliberately bounded to the UI's 30-message page size.
    const int pageLimit = qMin(limit, 30);
    const bool hasCursor = beforeOriginTs > 0;
    QString sql = QStringLiteral(
        "SELECT room_id, event_id, event_type, sender, origin_ts, message_type, content, metadata "
        "FROM timeline_events WHERE room_id = :room_id ");
    if (hasCursor)
        sql += QStringLiteral("AND (origin_ts < :before_ts OR "
                              "(origin_ts = :before_ts AND event_id < :before_event_id)) ");
    sql += QStringLiteral("ORDER BY origin_ts DESC, event_id DESC LIMIT :limit");

    QSqlQuery query(FDatabase);
    query.prepare(sql);
    query.bindValue(QStringLiteral(":room_id"), roomId);
    if (hasCursor) {
        query.bindValue(QStringLiteral(":before_ts"), beforeOriginTs);
        query.bindValue(QStringLiteral(":before_event_id"), beforeEventId);
    }
    query.bindValue(QStringLiteral(":limit"), pageLimit + 1);
    if (!query.exec())
        return result;

    result.success = true;
    int loaded = 0;
    while (query.next()) {
        if (loaded == pageLimit) {
            result.hasMore = true;
            break;
        }

        const QSqlRecord record = query.record();
        MatrixTimelineEvent event;
        event.roomId = record.value(QStringLiteral("room_id")).toString();
        event.eventId = record.value(QStringLiteral("event_id")).toString();
        event.eventType = record.value(QStringLiteral("event_type")).toString();
        event.sender = record.value(QStringLiteral("sender")).toString();
        event.originTs = record.value(QStringLiteral("origin_ts")).toLongLong();
        event.messageType = record.value(QStringLiteral("message_type")).toString();
        event.content = record.value(QStringLiteral("content")).toString();

        const QByteArray metadataJson = record.value(QStringLiteral("metadata")).toByteArray();
        if (!metadataJson.isEmpty()) {
            const QJsonDocument document = QJsonDocument::fromJson(metadataJson);
            if (document.isObject()) {
                const QJsonObject metadata = document.object();
                for (auto it = metadata.begin(); it != metadata.end(); ++it)
                    event.metadata.insert(it.key(), it.value().toVariant());
            }
        }

        // Rows are queried newest-first; prepend to return a chronological page.
        result.events.prepend(event);
        ++loaded;
    }
    return result;
}

QStringList MatrixDatabase::roomIds() const
{
   QStringList result;
   QSqlQuery query(FDatabase);
   if (!query.exec(QStringLiteral("SELECT DISTINCT room_id FROM timeline_events ORDER BY room_id")))
       return result;
   while (query.next())
       result.append(query.value(0).toString());
   return result;
}

QList<MatrixStoredRoom> MatrixDatabase::roomStates() const
{
    QList<MatrixStoredRoom> result;
    QSqlQuery query(FDatabase);
    if (!query.exec(QStringLiteral(
            "SELECT room_id, name, topic, avatar_url, membership, is_direct, is_encrypted, room_type "
            "FROM rooms ORDER BY name, room_id")))
        return result;
    while (query.next()) {
        MatrixStoredRoom room;
        room.roomId = query.value(0).toString();
        room.name = query.value(1).toString();
        room.topic = query.value(2).toString();
        room.avatarUrl = query.value(3).toString();
        room.membership = query.value(4).toString();
        room.isDirect = query.value(5).toBool();
        room.isEncrypted = query.value(6).toBool();
        room.roomType = query.value(7).toString();
        if (!room.roomId.isEmpty())
            result.append(room);
    }
    return result;
}

QList<MatrixStoredMember> MatrixDatabase::roomMembers(const QString &roomId) const
{
    QList<MatrixStoredMember> result;
    QSqlQuery query(FDatabase);
    query.prepare(QStringLiteral("SELECT user_id, membership, display_name, avatar_url FROM room_members WHERE room_id = :room_id ORDER BY user_id"));
    query.bindValue(QStringLiteral(":room_id"), roomId);
    if (!query.exec())
        return result;
    while (query.next()) {
        MatrixStoredMember member;
        member.userId = query.value(0).toString();
        member.membership = query.value(1).toString();
        member.displayName = query.value(2).toString();
        member.avatarUrl = query.value(3).toString();
        if (member.membership == QStringLiteral("join") && !member.userId.isEmpty())
            result.append(member);
    }
    return result;
}

bool MatrixDatabase::setSyncToken(const QString &roomId, qint64 token)
{
    QSqlQuery query(FDatabase);
    query.prepare("INSERT OR REPLACE INTO sync_state (room_id, sync_token) VALUES (:room_id, :token)");
    query.bindValue(":room_id", roomId);
    query.bindValue(":token", token);
    
    return query.exec();
}

qint64 MatrixDatabase::getSyncToken(const QString &roomId) const
{
    QSqlQuery query(FDatabase);
    query.prepare("SELECT sync_token FROM sync_state WHERE room_id = :room_id");
    query.bindValue(":room_id", roomId);
    
    if (!query.exec()) {
        return 0;
    }
    
    if (query.next()) {
        return query.value("sync_token").toLongLong();
    }
    
    return 0;
}

bool MatrixDatabase::setNextBatch(const QString &nextBatch)
{
    QSqlQuery query(FDatabase);
    if (nextBatch.isEmpty()) {
        if (!query.exec(QStringLiteral("DELETE FROM sync_cursor WHERE id = 1"))) {
            qWarning() << "Failed to clear Matrix sync cursor:"
                       << query.lastError().text()
                       << "database:" << FDatabase.databaseName()
                       << "open:" << FDatabase.isOpen();
            return false;
        }
        return true;
    }
    query.prepare("INSERT OR REPLACE INTO sync_cursor (id, next_batch) VALUES (1, :next_batch)");
    query.bindValue(QStringLiteral(":next_batch"), nextBatch);
    if (!query.exec()) {
        qWarning() << "Failed to set Matrix sync cursor:"
                   << query.lastError().text()
                   << "database:" << FDatabase.databaseName()
                   << "open:" << FDatabase.isOpen();
        return false;
    }
    return true;
}

QString MatrixDatabase::nextBatch() const
{
    QSqlQuery query(FDatabase);
    if (!query.exec(QStringLiteral("SELECT next_batch FROM sync_cursor WHERE id = 1")) || !query.next())
        return QString();
    return query.value(0).toString();
}

bool MatrixDatabase::saveSyncFilter(const QString &filterId)
{
    if (filterId.isEmpty())
        return false;
    QSqlQuery query(FDatabase);
    query.prepare(QStringLiteral(
        "INSERT INTO sync_filter (id, filter_id) VALUES (1, :filter_id) "
        "ON CONFLICT(id) DO UPDATE SET filter_id=excluded.filter_id"));
    query.bindValue(QStringLiteral(":filter_id"), filterId);
    return query.exec();
}

QString MatrixDatabase::syncFilter() const
{
    QSqlQuery query(FDatabase);
    if (!query.exec(QStringLiteral("SELECT filter_id FROM sync_filter WHERE id = 1")) || !query.next())
        return QString();
    return query.value(0).toString();
}

bool MatrixDatabase::removeSyncFilter()
{
    QSqlQuery query(FDatabase);
    return query.exec(QStringLiteral("DELETE FROM sync_filter WHERE id = 1"));
}

bool MatrixDatabase::saveRoomState(const QString &roomId, const QString &name,
                                   const QString &topic, const QString &avatarUrl,
                                   const QString &membership, bool isDirect,
                                   bool isEncrypted, const QString &replacementRoomId,
                                   const QString &prevBatch)
{
    QSqlQuery query(FDatabase);
    query.prepare(QStringLiteral(
        "INSERT INTO rooms "
        "(room_id, name, topic, avatar_url, membership, is_direct, is_encrypted, "
        "replacement_room_id, prev_batch) VALUES "
        "(:room_id, :name, :topic, :avatar_url, :membership, :is_direct, :is_encrypted, "
        ":replacement_room_id, :prev_batch) "
        "ON CONFLICT(room_id) DO UPDATE SET "
        "name=excluded.name, topic=excluded.topic, avatar_url=excluded.avatar_url, "
        "membership=excluded.membership, is_direct=excluded.is_direct, "
        "is_encrypted=excluded.is_encrypted, replacement_room_id=excluded.replacement_room_id, "
        "prev_batch=excluded.prev_batch"));
    query.bindValue(QStringLiteral(":room_id"), roomId);
    query.bindValue(QStringLiteral(":name"), name);
    query.bindValue(QStringLiteral(":topic"), topic);
    query.bindValue(QStringLiteral(":avatar_url"), avatarUrl);
    query.bindValue(QStringLiteral(":membership"), membership);
    query.bindValue(QStringLiteral(":is_direct"), isDirect ? 1 : 0);
    query.bindValue(QStringLiteral(":is_encrypted"), isEncrypted ? 1 : 0);
    query.bindValue(QStringLiteral(":replacement_room_id"), replacementRoomId);
    query.bindValue(QStringLiteral(":prev_batch"), prevBatch);
    return query.exec();
}

bool MatrixDatabase::saveRoomType(const QString &roomId, const QString &roomType,
                                  const QString &roomVersion)
{
    QSqlQuery query(FDatabase);
    query.prepare(QStringLiteral(
        "INSERT INTO rooms (room_id, room_type, room_version) "
        "VALUES (:room_id, :room_type, :room_version) "
        "ON CONFLICT(room_id) DO UPDATE SET "
        "room_type=excluded.room_type, room_version=excluded.room_version"));
    query.bindValue(QStringLiteral(":room_type"), roomType);
    query.bindValue(QStringLiteral(":room_version"), roomVersion);
    query.bindValue(QStringLiteral(":room_id"), roomId);
    return query.exec();
}

bool MatrixDatabase::saveRoomTimelineBoundary(const QString &roomId, const QString &prevBatch,
                                              bool limited)
{
    QSqlQuery query(FDatabase);
    query.prepare(QStringLiteral(
        "INSERT INTO rooms (room_id, prev_batch, limited) VALUES "
        "(:room_id, :prev_batch, :limited) "
        "ON CONFLICT(room_id) DO UPDATE SET prev_batch=excluded.prev_batch, "
        "limited=excluded.limited"));
    query.bindValue(QStringLiteral(":room_id"), roomId);
    query.bindValue(QStringLiteral(":prev_batch"), prevBatch);
    query.bindValue(QStringLiteral(":limited"), limited ? 1 : 0);
    return query.exec();
}

bool MatrixDatabase::loadRoomTimelineBoundary(const QString &roomId, QString &prevBatch,
                                              bool &limited) const
{
    QSqlQuery query(FDatabase);
    query.prepare(QStringLiteral("SELECT prev_batch, limited FROM rooms WHERE room_id = :room_id"));
    query.bindValue(QStringLiteral(":room_id"), roomId);
    if (!query.exec() || !query.next())
        return false;
    prevBatch = query.value(0).toString();
    limited = query.value(1).toInt() != 0;
    return true;
}

bool MatrixDatabase::saveRoomMember(const QString &roomId, const QString &userId,
                                    const QString &membership, const QString &displayName,
                                    const QString &avatarUrl, const QString &eventId)
{
    QSqlQuery query(FDatabase);
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO contacts (user_id, display_name, avatar_url) "
        "VALUES (:user_id, :display_name, :avatar_url)"));
    query.bindValue(QStringLiteral(":user_id"), userId);
    query.bindValue(QStringLiteral(":display_name"), displayName);
    query.bindValue(QStringLiteral(":avatar_url"), avatarUrl);
    if (!query.exec())
        return false;
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO room_members "
        "(room_id, user_id, membership, display_name, avatar_url, event_id) "
        "VALUES (:room_id, :user_id, :membership, :display_name, :avatar_url, :event_id)"));
    query.bindValue(QStringLiteral(":room_id"), roomId);
    query.bindValue(QStringLiteral(":user_id"), userId);
    query.bindValue(QStringLiteral(":membership"), membership);
    query.bindValue(QStringLiteral(":display_name"), displayName);
    query.bindValue(QStringLiteral(":avatar_url"), avatarUrl);
    query.bindValue(QStringLiteral(":event_id"), eventId);
    return query.exec();
}

bool MatrixDatabase::saveDirectRooms(const QSet<QString> &roomIds)
{
    if (!FDatabase.transaction())
        return false;
    QSqlQuery query(FDatabase);
    if (!query.exec(QStringLiteral("DELETE FROM direct_rooms"))) {
        FDatabase.rollback();
        return false;
    }
    query.prepare(QStringLiteral("INSERT OR IGNORE INTO direct_rooms (room_id) VALUES (:room_id)"));
    for (const QString &roomId : roomIds) {
        query.bindValue(QStringLiteral(":room_id"), roomId);
        if (!query.exec()) {
            FDatabase.rollback();
            return false;
        }
    }
    return FDatabase.commit();
}

QSet<QString> MatrixDatabase::directRooms() const
{
    QSet<QString> roomIds;
    QSqlQuery query(FDatabase);
    if (!query.exec(QStringLiteral("SELECT room_id FROM direct_rooms")))
        return roomIds;
    while (query.next())
        roomIds.insert(query.value(0).toString());
    return roomIds;
}

bool MatrixDatabase::saveRoomMemberHistory(const QString &roomId, const QString &userId,
                                           const QString &membership, const QString &displayName,
                                           const QString &avatarUrl, qint64 validFrom,
                                           const QString &eventId)
{
    QSqlQuery query(FDatabase);
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO room_member_history "
        "(room_id, user_id, membership, display_name, avatar_url, valid_from, event_id) "
        "VALUES (:room_id, :user_id, :membership, :display_name, :avatar_url, :valid_from, :event_id)"));
    query.bindValue(QStringLiteral(":room_id"), roomId);
    query.bindValue(QStringLiteral(":user_id"), userId);
    query.bindValue(QStringLiteral(":membership"), membership);
    query.bindValue(QStringLiteral(":display_name"), displayName);
    query.bindValue(QStringLiteral(":avatar_url"), avatarUrl);
    query.bindValue(QStringLiteral(":valid_from"), validFrom);
    query.bindValue(QStringLiteral(":event_id"), eventId);
    return query.exec();
}

QString MatrixDatabase::historicalMemberDisplayName(const QString &roomId, const QString &userId,
                                                    qint64 timestamp) const
{
    QSqlQuery query(FDatabase);
    query.prepare(QStringLiteral(
        "SELECT display_name FROM room_member_history "
        "WHERE room_id = :room_id AND user_id = :user_id AND valid_from <= :timestamp "
        "ORDER BY valid_from DESC LIMIT 1"));
    query.bindValue(QStringLiteral(":room_id"), roomId);
    query.bindValue(QStringLiteral(":user_id"), userId);
    query.bindValue(QStringLiteral(":timestamp"), timestamp);
    if (!query.exec() || !query.next())
        return QString();
    return query.value(0).toString();
}

bool MatrixDatabase::setReadMarkers(const QString &roomId, const QString &fullyReadEventId, const QString &readEventId)
{
    QSqlQuery query(FDatabase);
    query.prepare("INSERT OR REPLACE INTO room_state (room_id, fully_read_event_id, read_event_id) "
                  "VALUES (:room_id, :fully_read, :read_event)");
    query.bindValue(":room_id", roomId);
    query.bindValue(":fully_read", fullyReadEventId);
    query.bindValue(":read_event", readEventId);
    return query.exec();
}

bool MatrixDatabase::saveReceipt(const QString &roomId, const QString &userId,
                                 const QString &eventId, const QString &receiptType,
                                 qint64 timestamp)
{
    if (roomId.isEmpty() || userId.isEmpty() || eventId.isEmpty() || receiptType.isEmpty())
        return false;
    QSqlQuery query(FDatabase);
    query.prepare(QStringLiteral(
        "INSERT INTO room_receipts (room_id, user_id, event_id, receipt_type, timestamp) "
        "VALUES (:room_id, :user_id, :event_id, :receipt_type, :timestamp) "
        "ON CONFLICT(room_id, user_id, receipt_type) DO UPDATE SET "
        "event_id=excluded.event_id, timestamp=excluded.timestamp"));
    query.bindValue(QStringLiteral(":room_id"), roomId);
    query.bindValue(QStringLiteral(":user_id"), userId);
    query.bindValue(QStringLiteral(":event_id"), eventId);
    query.bindValue(QStringLiteral(":receipt_type"), receiptType);
    query.bindValue(QStringLiteral(":timestamp"), timestamp);
    return query.exec();
}

bool MatrixDatabase::saveRoomAccountData(const QString &roomId, const QString &eventType,
                                         const QByteArray &contentJson)
{
    if (roomId.isEmpty() || eventType.isEmpty())
        return false;
    QSqlQuery query(FDatabase);
    query.prepare(QStringLiteral(
        "INSERT INTO room_account_data (room_id, event_type, content_json) "
        "VALUES (:room_id, :event_type, :content_json) "
        "ON CONFLICT(room_id, event_type) DO UPDATE SET content_json=excluded.content_json"));
    query.bindValue(QStringLiteral(":room_id"), roomId);
    query.bindValue(QStringLiteral(":event_type"), eventType);
    query.bindValue(QStringLiteral(":content_json"), contentJson);
    return query.exec();
}

bool MatrixDatabase::saveOutboxMessage(const QString &roomId, const QString &transactionId,
                                       const QString &body, const QString &status)
{
    QSqlQuery query(FDatabase);
    query.prepare("INSERT OR REPLACE INTO outbox (transaction_id, room_id, body, status) "
                  "VALUES (:transaction_id, :room_id, :body, :status)");
    query.bindValue(":transaction_id", transactionId);
    query.bindValue(":room_id", roomId);
    query.bindValue(":body", body);
    query.bindValue(":status", status);
    return query.exec();
}

bool MatrixDatabase::removeOutboxMessage(const QString &transactionId)
{
    QSqlQuery query(FDatabase);
    query.prepare("DELETE FROM outbox WHERE transaction_id = :transaction_id");
    query.bindValue(":transaction_id", transactionId);
    return query.exec();
}

QList<MatrixOutboxEntry> MatrixDatabase::failedOutboxMessages() const
{
    QList<MatrixOutboxEntry> entries;
    QSqlQuery query(FDatabase);
    query.prepare("SELECT room_id, transaction_id, body, status FROM outbox WHERE status = 'failed'");
    if (!query.exec())
        return entries;
    while (query.next()) {
        MatrixOutboxEntry entry;
        entry.roomId = query.value(0).toString();
        entry.transactionId = query.value(1).toString();
        entry.body = query.value(2).toString();
        entry.status = query.value(3).toString();
        entries.append(entry);
    }
    return entries;
}

bool MatrixDatabase::saveSentNotification(const QString &roomId, const QString &eventId)
{
    QSqlQuery query(FDatabase);
    query.prepare("INSERT OR IGNORE INTO sent_notifications (event_id, room_id) "
                  "VALUES (:event_id, :room_id)");
    query.bindValue(":event_id", eventId);
    query.bindValue(":room_id", roomId);
    return query.exec();
}

bool MatrixDatabase::hasSentNotification(const QString &eventId) const
{
    QSqlQuery query(FDatabase);
    query.prepare("SELECT COUNT(*) FROM sent_notifications WHERE event_id = :event_id");
    query.bindValue(":event_id", eventId);
    if (!query.exec() || !query.next())
        return false;
    return query.value(0).toInt() > 0;
}

bool MatrixDatabase::removeSentNotifications(const QString &roomId)
{
    QSqlQuery query(FDatabase);
    query.prepare("DELETE FROM sent_notifications WHERE room_id = :room_id");
    query.bindValue(":room_id", roomId);
    return query.exec();
}

bool MatrixDatabase::saveOlmAccount(const QString &userId, const QString &deviceId,
                                    const QByteArray &pickleKey, const QByteArray &pickle)
{
    QSqlQuery query(FDatabase);
    query.prepare("INSERT OR REPLACE INTO olm_accounts "
                  "(user_id, device_id, pickle_key, pickle) "
                  "VALUES (:user_id, :device_id, :pickle_key, :pickle)");
    query.bindValue(":user_id", userId);
    query.bindValue(":device_id", deviceId);
    query.bindValue(":pickle_key", pickleKey);
    query.bindValue(":pickle", pickle);
    return query.exec();
}

bool MatrixDatabase::hasOlmAccount(const QString &userId, const QString &deviceId) const
{
    QSqlQuery query(FDatabase);
    query.prepare("SELECT 1 FROM olm_accounts "
                  "WHERE user_id = :user_id AND device_id = :device_id LIMIT 1");
    query.bindValue(":user_id", userId);
    query.bindValue(":device_id", deviceId);
    return query.exec() && query.next();
}

bool MatrixDatabase::loadOlmAccount(const QString &userId, const QString &deviceId,
                                    QByteArray &pickleKey, QByteArray &pickle) const
{
    QSqlQuery query(FDatabase);
    query.prepare("SELECT pickle_key, pickle FROM olm_accounts "
                  "WHERE user_id = :user_id AND device_id = :device_id");
    query.bindValue(":user_id", userId);
    query.bindValue(":device_id", deviceId);
    if (!query.exec() || !query.next())
        return false;
    pickleKey = query.value(0).toByteArray();
    pickle = query.value(1).toByteArray();
    return !pickleKey.isEmpty() && !pickle.isEmpty();
}

bool MatrixDatabase::saveMegolmSession(const QString &sessionId, const QString &roomId,
                                       const QString &senderKey, const QByteArray &pickle,
                                       const QString &claimedEd25519,
                                       const QString &forwardingChainJson)
{
    QSqlQuery query(FDatabase);
    query.prepare("INSERT OR REPLACE INTO olm_megolm_sessions "
                  "(session_id, room_id, sender_key, pickle, sender_claimed_ed25519, "
                  "forwarding_curve25519_key_chain) "
                  "VALUES (:session_id, :room_id, :sender_key, :pickle, "
                  "CASE WHEN :claimed_ed25519 <> '' THEN :claimed_ed25519 ELSE "
                  "COALESCE((SELECT sender_claimed_ed25519 FROM olm_megolm_sessions "
                  "WHERE session_id = :session_id), '') END, "
                  "CASE WHEN :forwarding_chain <> '' AND :forwarding_chain <> '[]' "
                  "THEN :forwarding_chain ELSE COALESCE((SELECT forwarding_curve25519_key_chain "
                  "FROM olm_megolm_sessions WHERE session_id = :session_id), '[]') END)");
    query.bindValue(":session_id", sessionId);
    query.bindValue(":room_id", roomId);
    query.bindValue(":sender_key", senderKey);
    query.bindValue(":pickle", pickle);
    query.bindValue(":claimed_ed25519", claimedEd25519);
    query.bindValue(":forwarding_chain",
                    forwardingChainJson.isEmpty() ? QStringLiteral("[]") : forwardingChainJson);
    return query.exec();
}

bool MatrixDatabase::loadMegolmSession(const QString &sessionId, QString &roomId,
                                       QString &senderKey, QByteArray &pickle,
                                       QString *claimedEd25519,
                                       QString *forwardingChainJson) const
{
    QSqlQuery query(FDatabase);
    query.prepare("SELECT room_id, sender_key, pickle, sender_claimed_ed25519, "
                  "forwarding_curve25519_key_chain FROM olm_megolm_sessions "
                  "WHERE session_id = :session_id");
    query.bindValue(":session_id", sessionId);
    if (!query.exec() || !query.next())
        return false;
    roomId = query.value(0).toString();
    senderKey = query.value(1).toString();
    pickle = query.value(2).toByteArray();
    if (claimedEd25519)
        *claimedEd25519 = query.value(3).toString();
    if (forwardingChainJson)
        *forwardingChainJson = query.value(4).toString();
    return !pickle.isEmpty();
}

bool MatrixDatabase::removeMegolmSession(const QString &sessionId)
{
    QSqlQuery query(FDatabase);
    query.prepare(QStringLiteral("DELETE FROM olm_megolm_sessions WHERE session_id = :session_id"));
    query.bindValue(QStringLiteral(":session_id"), sessionId);
    return query.exec();
}

bool MatrixDatabase::saveOutboundMegolmSession(const QString &roomId, const QString &sessionId,
                                               const QByteArray &pickle, qint64 createdAt)
{
    QSqlQuery query(FDatabase);
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO olm_outbound_megolm_sessions "
        "(room_id, session_id, pickle, created_at) VALUES (:room_id, :session_id, :pickle, "
        "CASE WHEN :created_at > 0 THEN :created_at ELSE "
        "COALESCE((SELECT created_at FROM olm_outbound_megolm_sessions WHERE room_id = :room_id), 0) END)"));
    query.bindValue(QStringLiteral(":room_id"), roomId);
    query.bindValue(QStringLiteral(":session_id"), sessionId);
    query.bindValue(QStringLiteral(":pickle"), pickle);
    query.bindValue(QStringLiteral(":created_at"), createdAt);
    return query.exec();
}

qint64 MatrixDatabase::outboundMegolmSessionCreatedAt(const QString &roomId) const
{
    QSqlQuery query(FDatabase);
    query.prepare(QStringLiteral(
        "SELECT created_at FROM olm_outbound_megolm_sessions WHERE room_id = :room_id"));
    query.bindValue(QStringLiteral(":room_id"), roomId);
    return query.exec() && query.next() ? query.value(0).toLongLong() : 0;
}

bool MatrixDatabase::loadOutboundMegolmSession(const QString &roomId, QString &sessionId,
                                               QByteArray &pickle) const
{
    QSqlQuery query(FDatabase);
    query.prepare(QStringLiteral(
        "SELECT session_id, pickle FROM olm_outbound_megolm_sessions WHERE room_id = :room_id"));
    query.bindValue(QStringLiteral(":room_id"), roomId);
    if (!query.exec() || !query.next())
        return false;
    sessionId = query.value(0).toString();
    pickle = query.value(1).toByteArray();
    return !sessionId.isEmpty() && !pickle.isEmpty();
}

bool MatrixDatabase::saveMegolmKeyShare(const QString &sessionId, const QString &userId,
                                        const QString &deviceId, quint32 minimumIndex)
{
    QSqlQuery query(FDatabase);
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO olm_megolm_key_shares "
        "(session_id, user_id, device_id, minimum_index) VALUES "
        "(:session_id, :user_id, :device_id, :minimum_index)"));
    query.bindValue(QStringLiteral(":session_id"), sessionId);
    query.bindValue(QStringLiteral(":user_id"), userId);
    query.bindValue(QStringLiteral(":device_id"), deviceId);
    query.bindValue(QStringLiteral(":minimum_index"), minimumIndex);
    return query.exec();
}

bool MatrixDatabase::wasMegolmKeyShared(const QString &sessionId, const QString &userId,
                                        const QString &deviceId) const
{
    QSqlQuery query(FDatabase);
    query.prepare(QStringLiteral(
        "SELECT 1 FROM olm_megolm_key_shares WHERE session_id = :session_id "
        "AND user_id = :user_id AND device_id = :device_id LIMIT 1"));
    query.bindValue(QStringLiteral(":session_id"), sessionId);
    query.bindValue(QStringLiteral(":user_id"), userId);
    query.bindValue(QStringLiteral(":device_id"), deviceId);
    return query.exec() && query.next();
}

int MatrixDatabase::megolmKeyShareIndex(const QString &sessionId, const QString &userId,
                                        const QString &deviceId) const
{
    QSqlQuery query(FDatabase);
    query.prepare(QStringLiteral(
        "SELECT minimum_index FROM olm_megolm_key_shares WHERE session_id = :session_id "
        "AND user_id = :user_id AND device_id = :device_id LIMIT 1"));
    query.bindValue(QStringLiteral(":session_id"), sessionId);
    query.bindValue(QStringLiteral(":user_id"), userId);
    query.bindValue(QStringLiteral(":device_id"), deviceId);
    return query.exec() && query.next() ? query.value(0).toInt() : -1;
}

QList<QPair<QString, QString>> MatrixDatabase::megolmKeyShareDevices(const QString &sessionId) const
{
    QList<QPair<QString, QString>> result;
    QSqlQuery query(FDatabase);
    query.prepare(QStringLiteral(
        "SELECT user_id, device_id FROM olm_megolm_key_shares WHERE session_id = :session_id"));
    query.bindValue(QStringLiteral(":session_id"), sessionId);
    if (!query.exec())
        return result;
    while (query.next())
        result.append(qMakePair(query.value(0).toString(), query.value(1).toString()));
    return result;
}

bool MatrixDatabase::saveKeyRequest(const QString &requestId, const QString &sender,
                                    const QString &deviceId, const QString &roomId,
                                    const QString &sessionId)
{
    QSqlQuery query(FDatabase);
    query.prepare("INSERT OR IGNORE INTO olm_key_requests "
                  "(request_id, sender, device_id, room_id, session_id) "
                  "VALUES (:request_id, :sender, :device_id, :room_id, :session_id)");
    query.bindValue(":request_id", requestId);
    query.bindValue(":sender", sender);
    query.bindValue(":device_id", deviceId);
    query.bindValue(":room_id", roomId);
    query.bindValue(":session_id", sessionId);
    return query.exec();
}

bool MatrixDatabase::hasKeyRequest(const QString &requestId) const
{
    QSqlQuery query(FDatabase);
    query.prepare("SELECT COUNT(*) FROM olm_key_requests WHERE request_id = :request_id");
    query.bindValue(":request_id", requestId);
    if (!query.exec() || !query.next())
        return false;
    return query.value(0).toInt() > 0;
}

bool MatrixDatabase::saveDeviceKeys(const QString &userId, const QString &deviceId,
                                    const QByteArray &keysJson)
{
    const QJsonObject deviceKeys = QJsonDocument::fromJson(keysJson).object();
    const QString identityKey = deviceKeys.value(QStringLiteral("keys")).toObject()
        .value(QStringLiteral("ed25519:%1").arg(deviceId)).toString();
    QString previousKey;
    QSqlQuery previous(FDatabase);
    previous.prepare(QStringLiteral(
        "SELECT identity_key FROM device_trust WHERE user_id = :user_id AND device_id = :device_id"));
    previous.bindValue(QStringLiteral(":user_id"), userId);
    previous.bindValue(QStringLiteral(":device_id"), deviceId);
    if (previous.exec() && previous.next())
        previousKey = previous.value(0).toString();

    QSqlQuery query(FDatabase);
    query.prepare("INSERT OR REPLACE INTO olm_device_keys "
                  "(user_id, device_id, keys_json) VALUES (:user_id, :device_id, :keys_json)");
    query.bindValue(":user_id", userId);
    query.bindValue(":device_id", deviceId);
    query.bindValue(":keys_json", keysJson);
    if (!query.exec())
        return false;

    QSqlQuery trust(FDatabase);
    trust.prepare(QStringLiteral(
        "INSERT INTO device_trust (user_id, device_id, identity_key, trust_state, key_changed) "
        "VALUES (:user_id, :device_id, :identity_key, 'unverified', :key_changed) "
        "ON CONFLICT(user_id, device_id) DO UPDATE SET "
        "identity_key=excluded.identity_key, "
        "trust_state=CASE WHEN device_trust.identity_key != excluded.identity_key THEN 'unverified' "
        "ELSE device_trust.trust_state END, "
        "key_changed=CASE WHEN device_trust.identity_key != excluded.identity_key THEN 1 "
        "ELSE device_trust.key_changed END"));
    trust.bindValue(QStringLiteral(":user_id"), userId);
    trust.bindValue(QStringLiteral(":device_id"), deviceId);
    trust.bindValue(QStringLiteral(":identity_key"), identityKey);
    trust.bindValue(QStringLiteral(":key_changed"),
                    !previousKey.isEmpty() && previousKey != identityKey ? 1 : 0);
    return trust.exec();
}

bool MatrixDatabase::removeDeviceKeysExcept(const QString &userId, const QStringList &deviceIds)
{
    if (userId.isEmpty())
        return false;
    QSqlQuery query(FDatabase);
    QStringList placeholders;
    for (int i = 0; i < deviceIds.size(); ++i)
        placeholders.append(QStringLiteral(":device_%1").arg(i));
    const QString condition = placeholders.isEmpty()
        ? QStringLiteral("1=1")
        : QStringLiteral("device_id NOT IN (%1)").arg(placeholders.join(QStringLiteral(",")));
    query.prepare(QStringLiteral("DELETE FROM olm_device_keys WHERE user_id = :user_id AND %1").arg(condition));
    query.bindValue(QStringLiteral(":user_id"), userId);
    for (int i = 0; i < deviceIds.size(); ++i)
        query.bindValue(placeholders.at(i), deviceIds.at(i));
    if (!query.exec())
        return false;

    QSqlQuery trust(FDatabase);
    trust.prepare(QStringLiteral("DELETE FROM device_trust WHERE user_id = :user_id AND %1").arg(condition));
    trust.bindValue(QStringLiteral(":user_id"), userId);
    for (int i = 0; i < deviceIds.size(); ++i)
        trust.bindValue(placeholders.at(i), deviceIds.at(i));
    if (!trust.exec())
        return false;

    QSqlQuery sessions(FDatabase);
    sessions.prepare(QStringLiteral("DELETE FROM olm_sessions_v2 WHERE user_id = :user_id AND %1").arg(condition));
    sessions.bindValue(QStringLiteral(":user_id"), userId);
    for (int i = 0; i < deviceIds.size(); ++i)
        sessions.bindValue(placeholders.at(i), deviceIds.at(i));
    if (!sessions.exec())
        return false;

    QSqlQuery shares(FDatabase);
    shares.prepare(QStringLiteral("DELETE FROM olm_megolm_key_shares WHERE user_id = :user_id AND %1").arg(condition));
    shares.bindValue(QStringLiteral(":user_id"), userId);
    for (int i = 0; i < deviceIds.size(); ++i)
        shares.bindValue(placeholders.at(i), deviceIds.at(i));
    return shares.exec();
}

bool MatrixDatabase::removeDeviceKeysForUser(const QString &userId)
{
    if (userId.isEmpty())
        return false;
    QSqlQuery query(FDatabase);
    query.prepare(QStringLiteral("DELETE FROM olm_device_keys WHERE user_id = :user_id"));
    query.bindValue(QStringLiteral(":user_id"), userId);
    if (!query.exec())
        return false;
    QSqlQuery trust(FDatabase);
    trust.prepare(QStringLiteral("DELETE FROM device_trust WHERE user_id = :user_id"));
    trust.bindValue(QStringLiteral(":user_id"), userId);
    if (!trust.exec())
        return false;
    QSqlQuery sessions(FDatabase);
    sessions.prepare(QStringLiteral("DELETE FROM olm_sessions_v2 WHERE user_id = :user_id"));
    sessions.bindValue(QStringLiteral(":user_id"), userId);
    if (!sessions.exec())
        return false;
    QSqlQuery shares(FDatabase);
    shares.prepare(QStringLiteral("DELETE FROM olm_megolm_key_shares WHERE user_id = :user_id"));
    shares.bindValue(QStringLiteral(":user_id"), userId);
    return shares.exec();
}

bool MatrixDatabase::loadDeviceKeys(const QString &userId, const QString &deviceId,
                                    QByteArray &keysJson) const
{
    QSqlQuery query(FDatabase);
    query.prepare("SELECT keys_json FROM olm_device_keys "
                  "WHERE user_id = :user_id AND device_id = :device_id");
    query.bindValue(":user_id", userId);
    query.bindValue(":device_id", deviceId);
    if (!query.exec() || !query.next())
        return false;
    keysJson = query.value(0).toByteArray();
    return !keysJson.isEmpty();
}

QMap<QString, QByteArray> MatrixDatabase::loadDeviceKeysForUser(const QString &userId) const
{
    QMap<QString, QByteArray> result;
    if (userId.isEmpty())
        return result;
    QSqlQuery query(FDatabase);
    query.prepare(QStringLiteral("SELECT device_id, keys_json FROM olm_device_keys WHERE user_id = :user_id"));
    query.bindValue(QStringLiteral(":user_id"), userId);
    if (!query.exec())
        return result;
    while (query.next())
        result.insert(query.value(0).toString(), query.value(1).toByteArray());
    return result;
}

bool MatrixDatabase::deviceKeyChanged(const QString &userId, const QString &deviceId) const
{
    QSqlQuery query(FDatabase);
    query.prepare(QStringLiteral(
        "SELECT key_changed FROM device_trust WHERE user_id = :user_id AND device_id = :device_id"));
    query.bindValue(QStringLiteral(":user_id"), userId);
    query.bindValue(QStringLiteral(":device_id"), deviceId);
    return query.exec() && query.next() && query.value(0).toInt() != 0;
}

bool MatrixDatabase::isDeviceVerified(const QString &userId, const QString &deviceId) const
{
    QSqlQuery query(FDatabase);
    query.prepare(QStringLiteral(
        "SELECT trust_state, key_changed FROM device_trust "
        "WHERE user_id = :user_id AND device_id = :device_id"));
    query.bindValue(QStringLiteral(":user_id"), userId);
    query.bindValue(QStringLiteral(":device_id"), deviceId);
    return query.exec() && query.next() && query.value(0).toString() == QStringLiteral("verified") &&
        query.value(1).toInt() == 0;
}

bool MatrixDatabase::userVerificationStates(const QStringList &userIds,
                                            QMap<QString, bool> &states) const
{
    states.clear();
    QStringList uniqueUserIds = userIds;
    uniqueUserIds.removeDuplicates();
    for (const QString &userId : uniqueUserIds)
        states.insert(userId, false);
    if (uniqueUserIds.isEmpty())
        return true;

    constexpr int batchSize = 500;
    for (int offset = 0; offset < uniqueUserIds.size(); offset += batchSize) {
        const QStringList batchUserIds = uniqueUserIds.mid(offset, batchSize);
        QStringList placeholders;
        for (int i = 0; i < batchUserIds.size(); ++i)
            placeholders.append(QStringLiteral(":user_%1").arg(i));

        QSqlQuery query(FDatabase);
        query.prepare(QStringLiteral(
            "SELECT keys.user_id, COUNT(*), "
            "SUM(CASE WHEN trust.trust_state = 'verified' AND trust.key_changed = 0 "
            "THEN 1 ELSE 0 END) "
            "FROM olm_device_keys AS keys "
            "LEFT JOIN device_trust AS trust ON trust.user_id = keys.user_id "
            "AND trust.device_id = keys.device_id "
            "WHERE keys.user_id IN (%1) GROUP BY keys.user_id")
            .arg(placeholders.join(QLatin1Char(','))));
        for (int i = 0; i < batchUserIds.size(); ++i)
            query.bindValue(placeholders.at(i), batchUserIds.at(i));
        if (!query.exec())
            return false;
        while (query.next()) {
            const int deviceCount = query.value(1).toInt();
            states.insert(query.value(0).toString(), deviceCount > 0 &&
                query.value(2).toInt() == deviceCount);
        }
        if (query.lastError().isValid())
            return false;
    }
    return true;
}

bool MatrixDatabase::saveCrossSigningKey(const QString &userId, const QString &keyType,
                                         const QByteArray &keyJson)
{
    if (userId.isEmpty() || keyType.isEmpty() || keyJson.isEmpty())
        return false;
    QSqlQuery query(FDatabase);
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO cross_signing_keys "
        "(user_id, key_type, key_json, updated_at) VALUES (:user_id, :key_type, :key_json, :updated_at)"));
    query.bindValue(QStringLiteral(":user_id"), userId);
    query.bindValue(QStringLiteral(":key_type"), keyType);
    query.bindValue(QStringLiteral(":key_json"), keyJson);
    query.bindValue(QStringLiteral(":updated_at"), QDateTime::currentSecsSinceEpoch());
    return query.exec();
}

bool MatrixDatabase::removeCrossSigningKey(const QString &userId, const QString &keyType)
{
    if (userId.isEmpty() || keyType.isEmpty())
        return false;
    QSqlQuery query(FDatabase);
    query.prepare(QStringLiteral(
        "DELETE FROM cross_signing_keys WHERE user_id = :user_id AND key_type = :key_type"));
    query.bindValue(QStringLiteral(":user_id"), userId);
    query.bindValue(QStringLiteral(":key_type"), keyType);
    return query.exec();
}

bool MatrixDatabase::loadCrossSigningKey(const QString &userId, const QString &keyType,
                                         QByteArray &keyJson) const
{
    QSqlQuery query(FDatabase);
    query.prepare(QStringLiteral(
        "SELECT key_json FROM cross_signing_keys WHERE user_id = :user_id AND key_type = :key_type"));
    query.bindValue(QStringLiteral(":user_id"), userId);
    query.bindValue(QStringLiteral(":key_type"), keyType);
    if (!query.exec() || !query.next())
        return false;
    keyJson = query.value(0).toByteArray();
    return !keyJson.isEmpty();
}

bool MatrixDatabase::hasCompleteCrossSigningKeys(const QString &userId) const
{
    QSqlQuery query(FDatabase);
    query.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM cross_signing_keys WHERE user_id = :user_id "
        "AND key_type IN ('master', 'self_signing', 'user_signing')"));
    query.bindValue(QStringLiteral(":user_id"), userId);
    return query.exec() && query.next() && query.value(0).toInt() == 3;
}

bool MatrixDatabase::saveVerificationState(const QString &transactionId, const QString &userId,
                                            const QString &deviceId, const QString &state,
                                            const QByteArray &eventJson)
{
    if (transactionId.isEmpty() || userId.isEmpty() || deviceId.isEmpty() || state.isEmpty())
        return false;
    QSqlQuery query(FDatabase);
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO sas_verifications "
        "(transaction_id, user_id, device_id, state, event_json, updated_at) "
        "VALUES (:transaction_id, :user_id, :device_id, :state, :event_json, :updated_at)"));
    query.bindValue(QStringLiteral(":transaction_id"), transactionId);
    query.bindValue(QStringLiteral(":user_id"), userId);
    query.bindValue(QStringLiteral(":device_id"), deviceId);
    query.bindValue(QStringLiteral(":state"), state);
    query.bindValue(QStringLiteral(":event_json"), eventJson);
    query.bindValue(QStringLiteral(":updated_at"), QDateTime::currentMSecsSinceEpoch());
    return query.exec();
}

bool MatrixDatabase::saveDeviceTrust(const QString &userId, const QString &deviceId,
                                     const QString &identityKey, const QString &trustState)
{
    if (userId.isEmpty() || deviceId.isEmpty() || identityKey.isEmpty() || trustState.isEmpty())
        return false;
    QSqlQuery query(FDatabase);
    query.prepare(QStringLiteral(
        "INSERT INTO device_trust (user_id, device_id, identity_key, trust_state, key_changed) "
        "VALUES (:user_id, :device_id, :identity_key, :trust_state, 0) "
        "ON CONFLICT(user_id, device_id) DO UPDATE SET identity_key = excluded.identity_key, "
        "trust_state = excluded.trust_state, key_changed = 0"));
    query.bindValue(QStringLiteral(":user_id"), userId);
    query.bindValue(QStringLiteral(":device_id"), deviceId);
    query.bindValue(QStringLiteral(":identity_key"), identityKey);
    query.bindValue(QStringLiteral(":trust_state"), trustState);
    return query.exec();
}

bool MatrixDatabase::saveOlmSession(const QString &userId, const QString &deviceId,
                                    const QString &sessionId, const QByteArray &pickle)
{
    QSqlQuery query(FDatabase);
    query.prepare("INSERT OR REPLACE INTO olm_sessions_v2 "
                  "(user_id, device_id, session_id, pickle) "
                  "VALUES (:user_id, :device_id, :session_id, :pickle)");
    query.bindValue(":user_id", userId);
    query.bindValue(":device_id", deviceId);
    query.bindValue(":session_id", sessionId);
    query.bindValue(":pickle", pickle);
    return query.exec();
}

bool MatrixDatabase::loadOlmSession(const QString &userId, const QString &deviceId,
                                    QString &sessionId, QByteArray &pickle) const
{
    QSqlQuery query(FDatabase);
    query.prepare("SELECT session_id, pickle FROM olm_sessions_v2 "
                  "WHERE user_id = :user_id AND device_id = :device_id "
                  "ORDER BY rowid DESC LIMIT 1");
    query.bindValue(":user_id", userId);
    query.bindValue(":device_id", deviceId);
    if (!query.exec() || !query.next())
        return false;
    sessionId = query.value(0).toString();
    pickle = query.value(1).toByteArray();
    return !pickle.isEmpty();
}

bool MatrixDatabase::clearOlmSessionsForUser(const QString &userId)
{
    QSqlQuery query(FDatabase);
    query.prepare(QStringLiteral("DELETE FROM olm_sessions_v2 WHERE user_id = :user_id"));
    query.bindValue(QStringLiteral(":user_id"), userId);
    return query.exec();
}

bool MatrixDatabase::removeOlmSessionsForDevice(const QString &userId, const QString &deviceId)
{
    QSqlQuery query(FDatabase);
    query.prepare(QStringLiteral(
        "DELETE FROM olm_sessions_v2 WHERE user_id = :user_id AND device_id = :device_id"));
    query.bindValue(QStringLiteral(":user_id"), userId);
    query.bindValue(QStringLiteral(":device_id"), deviceId);
    return query.exec();
}

bool MatrixDatabase::ensureOlmSessionContext(const QString &userId, const QString &deviceId,
                                              bool &changed)
{
    changed = false;
    QSqlQuery create(FDatabase);
    if (!create.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS olm_session_context ("
            "user_id TEXT PRIMARY KEY NOT NULL, device_id TEXT NOT NULL)")))
        return false;
    QSqlQuery select(FDatabase);
    select.prepare(QStringLiteral("SELECT device_id FROM olm_session_context WHERE user_id = :user_id"));
    select.bindValue(QStringLiteral(":user_id"), userId);
    if (!select.exec())
        return false;
    if (select.next()) {
        changed = select.value(0).toString() != deviceId;
        if (!changed)
            return true;
        QSqlQuery update(FDatabase);
        update.prepare(QStringLiteral(
            "UPDATE olm_session_context SET device_id = :device_id WHERE user_id = :user_id"));
        update.bindValue(QStringLiteral(":device_id"), deviceId);
        update.bindValue(QStringLiteral(":user_id"), userId);
        return update.exec();
    }
    QSqlQuery insert(FDatabase);
    insert.prepare(QStringLiteral(
        "INSERT INTO olm_session_context (user_id, device_id) VALUES (:user_id, :device_id)"));
    insert.bindValue(QStringLiteral(":user_id"), userId);
    insert.bindValue(QStringLiteral(":device_id"), deviceId);
    changed = true;
    return insert.exec();
}

bool MatrixDatabase::removeOlmSession(const QString &userId, const QString &deviceId,
                                       const QString &sessionId)
{
    QSqlQuery query(FDatabase);
    query.prepare(QStringLiteral(
        "DELETE FROM olm_sessions_v2 WHERE user_id = :user_id AND device_id = :device_id "
        "AND session_id = :session_id"));
    query.bindValue(QStringLiteral(":user_id"), userId);
    query.bindValue(QStringLiteral(":device_id"), deviceId);
    query.bindValue(QStringLiteral(":session_id"), sessionId);
    return query.exec();
}

QList<QPair<QString, QByteArray>> MatrixDatabase::loadOlmSessions(const QString &userId,
                                                                    const QString &deviceId) const
{
    QList<QPair<QString, QByteArray>> sessions;
    QSqlQuery query(FDatabase);
    query.prepare("SELECT session_id, pickle FROM olm_sessions_v2 "
                  "WHERE user_id = :user_id AND device_id = :device_id ORDER BY rowid DESC");
    query.bindValue(":user_id", userId);
    query.bindValue(":device_id", deviceId);
    if (!query.exec())
        return sessions;
    while (query.next()) {
        const QString sessionId = query.value(0).toString();
        const QByteArray pickle = query.value(1).toByteArray();
        if (!sessionId.isEmpty() && !pickle.isEmpty())
            sessions.append(qMakePair(sessionId, pickle));
    }
    return sessions;
}

bool MatrixDatabase::checkAndSaveMegolmMessageIndex(const QString &sessionId,
                                                     quint32 messageIndex,
                                                     const QString &eventId,
                                                     bool &replayDetected)
{
    replayDetected = false;
    if (sessionId.isEmpty() || eventId.isEmpty())
        return false;
    QSqlQuery query(FDatabase);
    query.prepare("SELECT event_id FROM olm_megolm_message_indices "
                  "WHERE session_id = :session_id AND message_index = :message_index");
    query.bindValue(":session_id", sessionId);
    query.bindValue(":message_index", messageIndex);
    if (!query.exec())
        return false;
    if (query.next()) {
        replayDetected = query.value(0).toString() != eventId;
        return true;
    }
    query.prepare("INSERT INTO olm_megolm_message_indices "
                  "(session_id, message_index, event_id) "
                  "VALUES (:session_id, :message_index, :event_id)");
    query.bindValue(":session_id", sessionId);
    query.bindValue(":message_index", messageIndex);
    query.bindValue(":event_id", eventId);
    return query.exec();
}

bool MatrixDatabase::updateSyncTokenOnEvent(const QString &roomId, const QString &eventId, qint64 ts)
{
    // Update the row's sync_token to reflect the latest received event
    QSqlQuery query(FDatabase);
    query.prepare(
        "UPDATE timeline_events SET sync_token = :sync_token "
        "WHERE room_id = :room_id AND event_id = :event_id"
    );
    query.bindValue(":room_id", roomId);
    query.bindValue(":event_id", eventId);
    query.bindValue(":sync_token", ts);
    
    return query.exec();
}

bool MatrixDatabase::clearOldEvents(const QString &roomId, int maxAgeDays)
{
    QSqlQuery query(FDatabase);
    query.prepare(
        "DELETE FROM timeline_events WHERE room_id = :room_id "
        "AND origin_ts < :cutoff_ts"
    );
    
    const qint64 cutoffTs = QDateTime::currentDateTime().addDays(-maxAgeDays).toMSecsSinceEpoch();
    query.bindValue(":room_id", roomId);
    query.bindValue(":cutoff_ts", cutoffTs);
    
    if (!query.exec()) {
        qWarning() << "Failed to clear old events:" << query.lastError();
        return false;
    }
    
    return true;
}