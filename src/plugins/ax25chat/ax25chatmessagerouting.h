#ifndef AX25CHATMESSAGE_ROUTING_H
#define AX25CHATMESSAGE_ROUTING_H

#include <interfaces/imessage.h>

#include <QDateTime>
#include <QString>
#include <QtGlobal>

namespace Ax25ChatMessageRouting
{
bool isRemoteCallsign(const QString &source, const QString &localCallsign);

BasicMessage createOutgoingMessage(const QString &destination,
                                   const QString &localCallsign,
                                   const QString &body,
                                   quint32 messageId,
                                   const QDateTime &timestamp);
}

#endif // AX25CHATMESSAGE_ROUTING_H
