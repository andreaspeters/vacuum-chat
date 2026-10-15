#include "matrixroommemberredactionpolicy.h"

#include <iostream>

namespace
{
bool expect(bool condition, const char *description)
{
    if (!condition)
        std::cerr << "FAIL: " << description << '\n';
    return condition;
}

MatrixRoomMemberRedactionPolicy::CachedEvent event(const QString &roomId,
    const QString &eventId, const QString &sender, const QString &eventType,
    bool redacted = false)
{
    return {roomId, eventId, sender, eventType, redacted};
}
}

int main()
{
    const QString room = QStringLiteral("!room:example.org");
    const QString member = QStringLiteral("@member:example.org");
    const QList<MatrixRoomMemberRedactionPolicy::CachedEvent> events{
        event(room, QStringLiteral("$message"), member, QStringLiteral("m.room.message")),
        event(room, QStringLiteral("$encrypted"), member, QStringLiteral("m.room.encrypted")),
        event(room, QStringLiteral("$sticker"), member, QStringLiteral("m.sticker")),
        event(room, QStringLiteral("$reaction"), member, QStringLiteral("m.reaction")),
        event(room, QStringLiteral("$other-sender"), QStringLiteral("@other:example.org"),
            QStringLiteral("m.room.message")),
        event(QStringLiteral("!other:example.org"), QStringLiteral("$other-room"), member,
            QStringLiteral("m.room.message")),
        event(room, QStringLiteral("$state"), member, QStringLiteral("m.room.topic")),
        event(room, QStringLiteral("$already-redacted"), member,
            QStringLiteral("m.room.message"), true),
        event(room, QString(), member, QStringLiteral("m.room.message")),
        event(room, QStringLiteral("$redaction"), member, QStringLiteral("m.room.redaction")),
        event(room, QStringLiteral("$message"), member, QStringLiteral("m.room.message"))
    };
    const QStringList ids = MatrixRoomMemberRedactionPolicy::eventIdsToRedact(events, room, member);
    bool passed = true;
    passed &= expect(ids == QStringList({QStringLiteral("$message"), QStringLiteral("$encrypted"),
        QStringLiteral("$sticker"), QStringLiteral("$reaction")}),
        "select unique, unredacted message-like events by exact room and sender");
    passed &= expect(MatrixRoomMemberRedactionPolicy::eventIdsToRedact(events, QString(), member).isEmpty(),
        "empty room identity selects no events");
    passed &= expect(MatrixRoomMemberRedactionPolicy::eventIdsToRedact(events, room, QString()).isEmpty(),
        "empty member identity selects no events");
    passed &= expect(MatrixRoomMemberRedactionPolicy::redactionPath(room,
        QStringLiteral("$event:example.org"), QStringLiteral("txn-1")) ==
        QStringLiteral("/_matrix/client/v3/rooms/%21room%3Aexample.org/redact/%24event%3Aexample.org/txn-1"),
        "build the Matrix v3 redaction endpoint from encoded path segments");
    return passed ? 0 : 1;
}
