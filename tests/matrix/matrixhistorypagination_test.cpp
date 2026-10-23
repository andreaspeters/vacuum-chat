#include "matrixdatabaseworker.h"
#include "matrixhistorybackup.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QVariantMap>

#include <iostream>

namespace
{
int fail(const char *message)
{
    std::cerr << message << '\n';
    return 1;
}

MatrixTimelineEvent makeEvent(const QString &roomId, const QString &eventId,
	qint64 timestamp, const QString &content)
{
	MatrixTimelineEvent event;
	event.roomId = roomId;
	event.eventId = eventId;
	event.eventType = QStringLiteral("m.room.message");
	event.sender = QStringLiteral("@sender:example.org");
	event.originTs = timestamp;
	event.messageType = QStringLiteral("m.text");
	event.content = content;
	return event;
}

bool hasExpectedRange(const QList<MatrixTimelineEvent> &events, int first, int last)
{
    if (events.size() != last - first + 1)
        return false;
    for (int i = 0; i < events.size(); ++i) {
        if (events.at(i).eventId != QStringLiteral("event-%1").arg(first + i, 3, 10,
                QLatin1Char('0')))
            return false;
    }
    return true;
}
}

int runHistoryPaginationTest()
{
	QTemporaryDir databaseProfileDirectory;
    if (!databaseProfileDirectory.isValid())
        return fail("could not create temporary profile directory");

    MatrixDatabase database;
    database.setProfileDirectory(databaseProfileDirectory.path());
    if (!database.openForAccount(QStringLiteral("https://example.org"),
                                 QStringLiteral("@test:example.org")))
        return fail("could not open temporary Matrix database");

    const QString roomId = QStringLiteral("!history:example.org");
    QList<MatrixTimelineEvent> events;
    for (int i = 0; i < 65; ++i) {
        // Adjacent events deliberately share timestamps to exercise the event-ID tie-breaker.
        const QString eventId = QStringLiteral("event-%1").arg(i, 3, 10, QLatin1Char('0'));
        events.append(makeEvent(roomId, eventId, 1700000000000LL + i / 2,
            QStringLiteral("message %1").arg(i)));
    }
    if (!database.appendTimelineEvents(roomId, events))
        return fail("could not seed synthetic timeline events");

    const MatrixHistoryPageResult latest = database.loadHistoryPage(roomId, 30);
    if (!latest.success || !latest.hasMore || !hasExpectedRange(latest.events, 35, 64))
        return fail("latest page must contain events 35..64 in chronological order");

    const MatrixHistoryPageResult oversized = database.loadHistoryPage(roomId, 1000);
    if (!oversized.success || !oversized.hasMore ||
        !hasExpectedRange(oversized.events, 35, 64))
        return fail("oversized request must remain bounded to the newest 30 events");

    const MatrixHistoryPageResult older = database.loadHistoryPage(roomId, 30,
        latest.events.first().originTs, latest.events.first().eventId);
    if (!older.success || !older.hasMore || !hasExpectedRange(older.events, 5, 34))
        return fail("older page must contain events 5..34 without crossing its cursor");

    const MatrixHistoryPageResult oldest = database.loadHistoryPage(roomId, 30,
        older.events.first().originTs, older.events.first().eventId);
    if (!oldest.success || oldest.hasMore || !hasExpectedRange(oldest.events, 0, 4))
        return fail("final page must contain the five oldest events and report exhaustion");

    const MatrixHistoryPageResult empty = database.loadHistoryPage(
        QStringLiteral("!empty:example.org"), 30);
    if (!empty.success || empty.hasMore || !empty.events.isEmpty())
        return fail("empty room must be a successful exhausted page");

    if (database.loadHistoryPage(roomId, 0).success)
        return fail("zero page size must be rejected");
    if (database.loadHistoryPage(roomId, 30, latest.events.first().originTs,
            QString()).success)
        return fail("incomplete keyset cursor must be rejected");

    const QString legacyImageRoomId = QStringLiteral("!legacy-image-cache:example.org");
    MatrixTimelineEvent legacyImage;
    legacyImage.roomId = legacyImageRoomId;
    legacyImage.eventId = QStringLiteral("$cached-image");
    legacyImage.eventType = QStringLiteral("m.room.message");
    legacyImage.sender = QStringLiteral("@sender:example.org");
    legacyImage.originTs = 1700000000100LL;
    legacyImage.messageType = QStringLiteral("m.image");
    legacyImage.content = QStringLiteral("image.png");
    const QString legacyImageUrl = QStringLiteral("mxc://media.example.org/cached-image");
    legacyImage.metadata.insert(QStringLiteral("url"), legacyImageUrl);
    if (!database.appendTimelineEvents(legacyImageRoomId, {legacyImage}))
        return fail("could not seed a legacy cached image without duplicated msgtype metadata");
    const MatrixHistoryPageResult cachedImagePage = database.loadHistoryPage(legacyImageRoomId, 30);
    if (!cachedImagePage.success || cachedImagePage.events.size() != 1 ||
        cachedImagePage.events.first().messageType != QStringLiteral("m.image") ||
        cachedImagePage.events.first().metadata.value(QStringLiteral("msgtype")).toString() !=
            QStringLiteral("m.image") ||
        cachedImagePage.events.first().metadata.value(QStringLiteral("url")).toString() != legacyImageUrl)
        return fail("legacy cached image page must restore both msgtype and media URL");
    const QList<MatrixTimelineEvent> cachedImageEvents = database.getEvents(legacyImageRoomId);
    if (cachedImageEvents.size() != 1 ||
        cachedImageEvents.first().metadata.value(QStringLiteral("msgtype")).toString() !=
            QStringLiteral("m.image") ||
        cachedImageEvents.first().metadata.value(QStringLiteral("url")).toString() != legacyImageUrl)
        return fail("SQLite room-history replay must restore image type and URL before rendering");

    database.close();
    if (database.loadHistoryPage(roomId, 30).success)
        return fail("closed database must report a history-page failure");

    QTemporaryDir workerProfileDirectory;
    if (!workerProfileDirectory.isValid())
        return fail("could not create temporary worker profile directory");
    MatrixDatabaseWorker worker;
    const QString serverUrl = QStringLiteral("https://worker.example.org");
    const QString userId = QStringLiteral("@worker:example.org");
    if (!worker.openForAccount(workerProfileDirectory.path(), serverUrl, userId))
        return fail("worker could not open its temporary Matrix database");
    bool workerSeeded = false;
    worker.execute([&](MatrixDatabase &workerDatabase) {
        workerSeeded = workerDatabase.appendTimelineEvents(roomId, events);
    });
    if (!workerSeeded)
        return fail("could not seed worker database");

    QList<MatrixTimelineEvent> workerInitialHistory;
    int historySignals = 0;
    QObject::connect(&worker, &MatrixDatabaseWorker::historyLoaded,
        [&](const QString &loadedRoomId, const QList<MatrixTimelineEvent> &loadedEvents) {
            if (loadedRoomId == roomId)
                workerInitialHistory = loadedEvents;
            ++historySignals;
        });
    worker.loadHistory(workerProfileDirectory.path(), serverUrl, userId, roomId);
    if (historySignals != 1 || !hasExpectedRange(workerInitialHistory, 35, 64))
        return fail("initial worker history must contain only the newest 30 events");

    MatrixHistoryPageResult workerPage;
    QString workerRoomId;
    int pageSignals = 0;
    QObject::connect(&worker, &MatrixDatabaseWorker::historyPageLoaded,
        [&](const QString &loadedRoomId, const MatrixHistoryPageResult &result) {
            workerRoomId = loadedRoomId;
            workerPage = result;
            ++pageSignals;
        });
    worker.loadHistoryPage(workerProfileDirectory.path(), serverUrl, userId, roomId, 30);
    if (pageSignals != 1 || workerRoomId != roomId || !workerPage.success ||
        !workerPage.hasMore || !hasExpectedRange(workerPage.events, 35, 64))
        return fail("worker must emit the newest 30 events in chronological order");

    worker.loadHistoryPage(workerProfileDirectory.path(), serverUrl, userId, roomId, 30,
        workerPage.events.first().originTs, workerPage.events.first().eventId);
    if (pageSignals != 2 || !workerPage.success || !workerPage.hasMore ||
        !hasExpectedRange(workerPage.events, 5, 34))
        return fail("worker must emit the next older page without crossing its cursor");

    worker.loadHistoryPage(workerProfileDirectory.path(), serverUrl, userId, roomId, 30,
        workerPage.events.first().originTs, workerPage.events.first().eventId);
    if (pageSignals != 3 || !workerPage.success || workerPage.hasMore ||
        !hasExpectedRange(workerPage.events, 0, 4))
        return fail("worker must report exhaustion after the oldest page");

    worker.loadHistoryPage(workerProfileDirectory.path(), serverUrl, userId, roomId, 0);
    if (pageSignals != 4 || workerPage.success)
        return fail("worker must signal an invalid page request as a failure");

    std::cout << "Matrix history pagination tests passed\n";
    return 0;
}

int runHistoryBackfillTest()
{
    QTemporaryDir profileDirectory;
    if (!profileDirectory.isValid())
        return fail("could not create temporary profile directory");

    MatrixDatabase database;
    database.setProfileDirectory(profileDirectory.path());
    if (!database.openForAccount(QStringLiteral("https://example.org"),
            QStringLiteral("@test:example.org")))
        return fail("could not open temporary Matrix database");

    const QString roomId = QStringLiteral("!history:example.org");
    if (!database.saveRoomTimelineBoundary(roomId, QStringLiteral("cursor-before"), false))
        return fail("could not seed initial server history cursor");

    const MatrixTimelineEvent event = makeEvent(roomId, QStringLiteral("$history-1"),
        1700000000000LL, QStringLiteral("historical message"));
    if (!database.persistHistoryBackfillPage(roomId, {event},
            QStringLiteral("cursor-after"), false))
        return fail("could not atomically persist a backfill event and cursor");

    QString cursor;
    bool limited = true;
    if (!database.loadRoomTimelineBoundary(roomId, cursor, limited) ||
        cursor != QStringLiteral("cursor-after") || limited)
        return fail("persisted backfill cursor was not readable");

    const MatrixHistoryPageResult page = database.loadHistoryPage(roomId, 30);
    if (!page.success || page.events.size() != 1 ||
        page.events.first().eventId != event.eventId ||
        page.events.first().content != event.content)
        return fail("persisted backfill event was not readable from the local history page");

    MatrixTimelineEvent invalid = makeEvent(roomId, QString(),
        1700000001000LL, QStringLiteral("historical message"));
    if (database.persistHistoryBackfillPage(roomId, {invalid},
            QStringLiteral("must-not-advance"), false))
        return fail("invalid backfill event batch was accepted");
    if (!database.loadRoomTimelineBoundary(roomId, cursor, limited) ||
        cursor != QStringLiteral("cursor-after"))
        return fail("failed event persistence advanced the server history cursor");

    if (!database.persistHistoryBackfillPage(roomId, {}, QString(), false))
        return fail("could not persist server history exhaustion");
    if (!database.loadRoomTimelineBoundary(roomId, cursor, limited) || !cursor.isEmpty())
        return fail("server history exhaustion was not persisted as an empty cursor");

    database.close();
    return 0;
}

int runPendingEncryptedEventQueryTest()
{
    QTemporaryDir profileDirectory;
    if (!profileDirectory.isValid())
        return fail("could not create temporary profile directory");

    MatrixDatabase database;
    database.setProfileDirectory(profileDirectory.path());
    if (!database.openForAccount(QStringLiteral("https://example.org"),
            QStringLiteral("@test:example.org")))
        return fail("could not open temporary Matrix database");

    const QString roomId = QStringLiteral("!encrypted:example.org");
    QList<MatrixTimelineEvent> events;
    for (int i = 0; i < 200; ++i)
        events.append(makeEvent(roomId, QStringLiteral("$plain-%1").arg(i),
            1700000000000LL + i, QStringLiteral("ordinary message")));
    const QStringList sessionIds = {QStringLiteral("target-session"),
        QStringLiteral("other-session"), QStringLiteral("target-session")};
    for (int i = 0; i < sessionIds.size(); ++i) {
        MatrixTimelineEvent encrypted = makeEvent(roomId,
            QStringLiteral("$encrypted-%1").arg(i), 1700000001000LL + i, QString());
        encrypted.eventType = QStringLiteral("m.room.encrypted");
        encrypted.metadata.insert(QStringLiteral("session_id"), sessionIds.at(i));
        encrypted.metadata.insert(QStringLiteral("ciphertext"), QStringLiteral("opaque"));
        events.append(encrypted);
    }
    if (!database.appendTimelineEvents(roomId, events))
        return fail("could not seed encrypted event query fixture");

    const QList<MatrixTimelineEvent> matching =
        database.pendingEncryptedEvents(roomId, QStringLiteral("target-session"));
    if (matching.size() != 2 ||
        matching.at(0).metadata.value(QStringLiteral("session_id")).toString() !=
            QStringLiteral("target-session") ||
        matching.at(1).metadata.value(QStringLiteral("session_id")).toString() !=
            QStringLiteral("target-session"))
        return fail("session retry query must return only matching encrypted events");

    if (database.pendingEncryptedEvents(roomId).size() != 3)
        return fail("room retry query must exclude plain timeline events");

    database.close();
    return 0;
}

namespace
{
bool check(bool condition, const char *message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

MatrixTextEvent event(const QString &room, const QString &id, const QString &sender,
    const QString &body, const QString &timestamp)
{
    MatrixTextEvent result;
    result.eventId = id;
    result.roomId = room;
    result.userId = sender;
    result.content = body;
    result.timestamp = timestamp;
    result.eventType = QStringLiteral("m.room.message");
    result.messageType = QStringLiteral("m.text");
    result.attachments = {QStringLiteral("mxc://example.org/file")};
    result.metadata.insert(QStringLiteral("formatted_body"), QStringLiteral("<b>backup</b>"));
    result.metadata.insert(QStringLiteral("sender_is_self"), sender == QStringLiteral("@me:example.org"));
    return result;
}
}

int runMatrixHistoryBackupTests()
{
    QTemporaryDir tempDir;
    if (!check(tempDir.isValid(), "temporary profile directory is available"))
        return 1;

    const QString roomId = QStringLiteral("!room:example.org");
    const QString otherRoomId = QStringLiteral("!other:example.org");
    const MatrixHistoryBackup backup(tempDir.path(), QStringLiteral("https://example.org"),
        QStringLiteral("@me:example.org"));
    const MatrixHistoryBackup otherAccount(tempDir.path(), QStringLiteral("https://example.org"),
        QStringLiteral("@other:example.org"));

    QList<MatrixTextEvent> messages = {
        event(roomId, QStringLiteral("$late"), QStringLiteral("@me:example.org"),
            QStringLiteral("later"), QStringLiteral("2000")),
        event(roomId, QStringLiteral("$early"), QStringLiteral("@alice:example.org"),
            QStringLiteral("earlier"), QStringLiteral("1000")),
        event(roomId, QStringLiteral("$early"), QStringLiteral("@alice:example.org"),
            QStringLiteral("duplicate"), QStringLiteral("1000")),
        event(otherRoomId, QStringLiteral("$other-room"), QStringLiteral("@alice:example.org"),
            QStringLiteral("wrong room"), QStringLiteral("1500"))
    };

    if (!check(backup.saveMessageHistory(roomId, messages), "backup snapshot is written atomically"))
        return 1;

    QList<MatrixTextEvent> loaded;
    if (!check(backup.loadMessageHistory(roomId, loaded), "backup snapshot can be read") ||
        !check(loaded.size() == 2, "room filter and event-ID deduplication are applied") ||
        !check(loaded.at(0).eventId == QStringLiteral("$early") &&
            loaded.at(0).content == QStringLiteral("earlier") &&
            loaded.at(1).eventId == QStringLiteral("$late"),
            "messages are ordered chronologically and preserve content") ||
        !check(loaded.at(0).userId == QStringLiteral("@alice:example.org") &&
            loaded.at(0).eventType == QStringLiteral("m.room.message") &&
            loaded.at(0).messageType == QStringLiteral("m.text") &&
            loaded.at(0).attachments.size() == 1 &&
            loaded.at(0).metadata.value(QStringLiteral("formatted_body")).toString() ==
                QStringLiteral("<b>backup</b>"),
            "sender and render metadata round-trip") ||
        !check(loaded.at(0).toBasicMessage().direction() == BasicMessage::Incoming &&
            loaded.at(1).toBasicMessage().direction() == BasicMessage::Outgoing,
            "backup conversion preserves Matrix sender direction"))
        return 1;

    if (!check(backup.backupFilePath(roomId) != backup.backupFilePath(otherRoomId) &&
        backup.backupFilePath(roomId) != otherAccount.backupFilePath(roomId),
        "backup files are isolated by room and account"))
        return 1;

#if defined(Q_OS_UNIX)
    const QFile::Permissions permissions = QFileInfo(backup.backupFilePath(roomId)).permissions();
    if (!check(!(permissions & (QFileDevice::ReadGroup | QFileDevice::WriteGroup |
            QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::WriteOther |
            QFileDevice::ExeOther)), "backup file is not accessible by group or other users"))
        return 1;
#endif

    QList<MatrixTextEvent> missing;
    if (!check(!backup.loadMessageHistory(otherRoomId, missing) && missing.isEmpty(),
        "missing room backup is reported without returning stale events"))
        return 1;

    std::cout << "Matrix history backup tests passed\n";
    return 0;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const int backupResult = runMatrixHistoryBackupTests();
    const int paginationResult = runHistoryPaginationTest();
    const int backfillResult = runHistoryBackfillTest();
    const int encryptedQueryResult = runPendingEncryptedEventQueryTest();
    return backupResult == 0 && paginationResult == 0 && backfillResult == 0 &&
        encryptedQueryResult == 0 ? 0 : 1;
}
