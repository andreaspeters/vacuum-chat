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
                                   quint32 messageId,
                                   const QDateTime &timestamp)
{
    const QString id = QStringLiteral("ax25:tx:%1:%2")
        .arg(destination, QString::number(messageId, 16).rightJustified(8, QLatin1Char('0')));
    return BasicMessage(id, destination, localCallsign, destination, body, timestamp,
                        QStringLiteral("ax25"), BasicMessage::Outgoing);
}
}
