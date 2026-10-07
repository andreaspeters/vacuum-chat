#ifndef PROTOCOLSOFTWAREVERSIONPOLICY_H
#define PROTOCOLSOFTWAREVERSIONPOLICY_H

#include <interfaces/iprotocolcapabilities.h>

namespace ClientInfoPolicy
{
inline bool canQuerySoftwareVersion(const IProtocolCapabilities *capabilities,
    const AccountId &accountId, const ConversationId &targetId)
{
    return capabilities && !accountId.isEmpty() && !targetId.isEmpty()
        && capabilities->hasCapabilities(accountId,
            IProtocolCapabilities::CapabilityQuerySoftwareVersion, targetId);
}
}

#endif // PROTOCOLSOFTWAREVERSIONPOLICY_H
