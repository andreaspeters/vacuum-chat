#ifndef MATRIXROOMMEMBERREDACTIONPOLICY_H
#define MATRIXROOMMEMBERREDACTIONPOLICY_H

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>

namespace MatrixRoomMemberRedactionPolicy
{
struct CachedEvent
{
    QString roomId;
    QString eventId;
    QString sender;
    QString eventType;
    bool redacted = false;
};

inline QStringList eventIdsToRedact(const QList<CachedEvent> &events,
    const QString &roomId, const QString &memberId)
{
    if (roomId.isEmpty() || memberId.isEmpty())
        return {};

    QStringList eventIds;
    QSet<QString> seen;
    for (const CachedEvent &event : events) {
        if (event.roomId != roomId || event.sender != memberId || event.eventId.isEmpty() ||
            event.redacted || seen.contains(event.eventId))
            continue;

        const bool messageLike = event.eventType == QStringLiteral("m.room.message") ||
            event.eventType == QStringLiteral("m.room.encrypted") ||
            event.eventType == QStringLiteral("m.sticker") ||
            event.eventType == QStringLiteral("m.reaction");
        if (!messageLike)
            continue;

        seen.insert(event.eventId);
        eventIds.append(event.eventId);
    }
    return eventIds;
}

inline QString redactionPath(const QString &roomId, const QString &eventId,
    const QString &transactionId)
{
    if (roomId.isEmpty() || eventId.isEmpty() || transactionId.isEmpty())
        return QString();
    return QStringLiteral("/_matrix/client/v3/rooms/%1/redact/%2/%3")
        .arg(QString::fromLatin1(QUrl::toPercentEncoding(roomId)),
            QString::fromLatin1(QUrl::toPercentEncoding(eventId)),
            QString::fromLatin1(QUrl::toPercentEncoding(transactionId)));
}
}

#endif // MATRIXROOMMEMBERREDACTIONPOLICY_H
