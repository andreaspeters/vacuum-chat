#include "matrixdatabaseworker.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <iostream>

namespace
{
int fail(const char *message)
{
    std::cerr << message << '\n';
    return 1;
}

MatrixTimelineEvent makeEvent(const QString &roomId, int index)
{
    MatrixTimelineEvent event;
    event.roomId = roomId;
    event.eventId = QStringLiteral("event-%1").arg(index, 3, 10, QLatin1Char('0'));
    event.eventType = QStringLiteral("m.room.message");
    event.sender = QStringLiteral("@sender:example.org");
    // Adjacent events deliberately share timestamps to exercise the event-ID tie-breaker.
    event.originTs = 1700000000000LL + index / 2;
    event.messageType = QStringLiteral("m.text");
    event.content = QStringLiteral("message %1").arg(index);
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

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
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
    for (int i = 0; i < 65; ++i)
        events.append(makeEvent(roomId, i));
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
