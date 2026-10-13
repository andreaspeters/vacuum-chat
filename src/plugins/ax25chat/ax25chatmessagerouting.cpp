#include "ax25chatmessagerouting.h"

namespace Ax25ChatMessageRouting
{
bool isRemoteCallsign(const QString &source, const QString &localCallsign)
{
    return !source.trimmed().isEmpty() && !localCallsign.trimmed().isEmpty() &&
        source.compare(localCallsign, Qt::CaseInsensitive) != 0;
}

BasicMessage createOutgoingMessage(const QString &destination,
                                   const QString &localCallsign,
                                   const QString &body,
                                   const QString &messageId,
                                   const QDateTime &timestamp)
{
    const QString id = QStringLiteral("aprs:tx:%1:%2").arg(destination, messageId);
    return BasicMessage(id, destination, localCallsign, destination, body, timestamp,
                        QStringLiteral("ax25"), BasicMessage::Outgoing);
}
}
