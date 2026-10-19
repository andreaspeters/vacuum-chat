#ifndef PROTOCOLHISTORYVIEWMODEL_H
#define PROTOCOLHISTORYVIEWMODEL_H

#include <interfaces/identity.h>
#include <interfaces/imessage.h>

#include <QDate>
#include <QList>
#include <QString>

struct ProtocolHistoryDayGroup
{
    QDate date;
    QList<BasicMessage> messages;
};

namespace ProtocolHistoryViewModel
{
QList<ProtocolHistoryDayGroup> groupByDay(const QList<BasicMessage> &messages,
    const ConversationId &conversationId, const QString &filter);
}

#endif // PROTOCOLHISTORYVIEWMODEL_H
