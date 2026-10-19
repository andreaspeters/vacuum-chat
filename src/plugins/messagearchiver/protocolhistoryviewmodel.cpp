#include "protocolhistoryviewmodel.h"

#include <QMap>
#include <algorithm>

namespace ProtocolHistoryViewModel
{
QList<ProtocolHistoryDayGroup> groupByDay(const QList<BasicMessage> &messages,
    const ConversationId &conversationId, const QString &filter)
{
    QMap<QDate, QList<BasicMessage> > grouped;
    const QString normalizedFilter = filter.trimmed();
    for (const BasicMessage &message : messages)
    {
        if (message.conversationId() != conversationId || !message.timestamp().isValid())
            continue;
        if (!normalizedFilter.isEmpty() &&
            !message.body().contains(normalizedFilter, Qt::CaseInsensitive) &&
            !message.sender().contains(normalizedFilter, Qt::CaseInsensitive) &&
            !message.recipient().contains(normalizedFilter, Qt::CaseInsensitive))
            continue;
        grouped[message.timestamp().date()].append(message);
    }

    QList<ProtocolHistoryDayGroup> result;
    for (auto it = grouped.begin(); it != grouped.end(); ++it)
    {
        std::stable_sort(it.value().begin(), it.value().end(),
            [](const BasicMessage &left, const BasicMessage &right) {
                return left.timestamp() < right.timestamp();
            });
        ProtocolHistoryDayGroup group;
        group.date = it.key();
        group.messages = it.value();
        result.append(group);
    }
    return result;
}
}
