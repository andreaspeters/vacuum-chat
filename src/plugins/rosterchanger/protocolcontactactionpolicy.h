#ifndef PROTOCOLCONTACTACTIONPOLICY_H
#define PROTOCOLCONTACTACTIONPOLICY_H

#include <interfaces/iprotocolcapabilities.h>
#include <interfaces/iprotocolcontactactions.h>
#include <interfaces/iprotocoladvertactions.h>

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

inline IProtocolCapabilities::Capability selfAdvertCapability(
    IProtocolAdvertActions::AdvertType type)
{
    switch (type) {
    case IProtocolAdvertActions::AdvertType::ZeroHop:
        return IProtocolCapabilities::CapabilitySendZeroHopAdvert;
    case IProtocolAdvertActions::AdvertType::Flood:
        return IProtocolCapabilities::CapabilitySendFloodAdvert;
    }
    return IProtocolCapabilities::CapabilityNone;
}

inline bool canOfferSelfAdvertAction(const IProtocolCapabilities *capabilities,
    const IProtocolAdvertActions *actions, const AccountId &accountId,
    IProtocolAdvertActions::AdvertType type)
{
    const IProtocolCapabilities::Capability capability = selfAdvertCapability(type);
    return capabilities && actions && !accountId.isEmpty() &&
        capability != IProtocolCapabilities::CapabilityNone &&
        capabilities->hasCapabilities(accountId, capability);
}

inline bool dispatchSelfAdvertAction(const IProtocolCapabilities *capabilities,
    IProtocolAdvertActions *actions, const AccountId &accountId,
    IProtocolAdvertActions::AdvertType type)
{
    return canOfferSelfAdvertAction(capabilities, actions, accountId, type) &&
        actions->sendSelfAdvert(accountId, type);
}

#endif // PROTOCOLCONTACTACTIONPOLICY_H
