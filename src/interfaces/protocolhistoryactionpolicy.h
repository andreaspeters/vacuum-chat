#ifndef PROTOCOLHISTORYACTIONPOLICY_H
#define PROTOCOLHISTORYACTIONPOLICY_H

#include <interfaces/identity.h>
#include <interfaces/iprotocolcapabilities.h>

namespace ProtocolHistoryActionPolicy
{
inline bool canViewLocalConversationHistory(const IProtocolCapabilities *capabilities,
    const AccountId &accountId, const QString &providerStreamId,
    const QString &rosterStreamId, const ConversationId &conversationId)
{
    return capabilities && !providerStreamId.isEmpty() &&
        providerStreamId == rosterStreamId && !conversationId.isEmpty() &&
        capabilities->hasCapabilities(accountId,
            IProtocolCapabilities::CapabilityViewHistory, conversationId);
}

inline bool canManageRemoteArchive(const IProtocolCapabilities *capabilities,
    const AccountId &accountId)
{
    return capabilities && capabilities->hasCapabilities(accountId,
        IProtocolCapabilities::CapabilityManageRemoteArchive);
}
}

#endif // PROTOCOLHISTORYACTIONPOLICY_H
