#ifndef PROTOCOLCONTACTACTIONPOLICY_H
#define PROTOCOLCONTACTACTIONPOLICY_H

#include <interfaces/iprotocolcapabilities.h>
#include <interfaces/iprotocolcontactactions.h>

inline bool canOfferAddContactAction(const IProtocolCapabilities *capabilities,
    const IProtocolContactActions *actions, const AccountId &accountId)
{
    return capabilities && actions && !accountId.isEmpty() &&
        capabilities->hasCapabilities(accountId, IProtocolCapabilities::CapabilityAddContact);
}

inline bool dispatchAddContactAction(const IProtocolCapabilities *capabilities,
    IProtocolContactActions *actions, const AccountId &accountId)
{
    return canOfferAddContactAction(capabilities, actions, accountId) &&
        actions->showAddContactDialog(accountId);
}

#endif // PROTOCOLCONTACTACTIONPOLICY_H
