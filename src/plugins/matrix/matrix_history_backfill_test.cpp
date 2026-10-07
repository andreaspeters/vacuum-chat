#include "matrixdatabase.h"

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

MatrixTimelineEvent makeEvent(const QString &roomId, const QString &eventId, qint64 timestamp)
{
    MatrixTimelineEvent event;
    event.roomId = roomId;
    event.eventId = eventId;
    event.eventType = QStringLiteral("m.room.message");
    event.sender = QStringLiteral("@sender:example.org");
    event.originTs = timestamp;
    event.messageType = QStringLiteral("m.text");
    event.content = QStringLiteral("historical message");
    return event;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
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
        1700000000000LL);
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

    MatrixTimelineEvent invalid = makeEvent(roomId, QString(), 1700000001000LL);
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
