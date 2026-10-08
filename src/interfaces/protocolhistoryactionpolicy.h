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

inline bool canViewLocalConversationHistoryForWindow(const IProtocolCapabilities *capabilities,
    const AccountId &windowAccountId, const ConversationId &windowConversationId,
    const AccountId &providerAccountId, const QString &providerStreamId,
    const QString &rosterStreamId)
{
    return !windowAccountId.isEmpty() && !windowConversationId.isEmpty() &&
        windowAccountId == providerAccountId &&
        canViewLocalConversationHistory(capabilities, providerAccountId,
            providerStreamId, rosterStreamId, windowConversationId);
}

inline bool canManageRemoteArchive(const IProtocolCapabilities *capabilities,
    const AccountId &accountId)
{
    return capabilities && capabilities->hasCapabilities(accountId,
        IProtocolCapabilities::CapabilityManageRemoteArchive);
}
}

#endif // PROTOCOLHISTORYACTIONPOLICY_H
