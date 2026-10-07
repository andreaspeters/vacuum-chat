#ifndef PROTOCOLPROFILEACTIONPOLICY_H
#define PROTOCOLPROFILEACTIONPOLICY_H

#include <QString>
#include <interfaces/identity.h>
#include <interfaces/iprotocolcapabilities.h>
#include <interfaces/iprotocolprofileactions.h>

namespace ProtocolProfileActionPolicy
{
inline bool canShowProfile(const IProtocolCapabilities *capabilities,
    const IProtocolProfileActions *actions, const AccountId &accountId,
    const UserId &userId)
{
    return capabilities && actions && !accountId.isEmpty() && !userId.isEmpty() &&
        capabilities->hasCapabilities(accountId,
            IProtocolCapabilities::CapabilityShowProfile, userId);
}

inline bool canEditProfile(const IProtocolCapabilities *capabilities,
    const IProtocolProfileActions *actions, const AccountId &accountId)
{
    return capabilities && actions && !accountId.isEmpty() &&
        capabilities->hasCapabilities(accountId,
            IProtocolCapabilities::CapabilityEditProfile);
}

inline bool matchesProviderBinding(bool hasRosterProvider,
    const AccountId &providerAccountId, const QString &providerStreamId,
    const AccountId &requestedAccountId, const QString &requestedStreamId)
{
    if (requestedAccountId.isEmpty())
        return false;
    if (!hasRosterProvider)
        return true;
    return !requestedStreamId.isEmpty() && providerAccountId == requestedAccountId &&
        providerStreamId == requestedStreamId;
}

inline bool dispatchShowProfile(const IProtocolCapabilities *capabilities,
    IProtocolProfileActions *actions, const AccountId &accountId,
    const UserId &userId)
{
    return canShowProfile(capabilities, actions, accountId, userId) &&
        actions->showProfile(accountId, userId);
}

inline bool dispatchEditProfile(const IProtocolCapabilities *capabilities,
    IProtocolProfileActions *actions, const AccountId &accountId)
{
    return canEditProfile(capabilities, actions, accountId) &&
        actions->editProfile(accountId);
}

} // namespace ProtocolProfileActionPolicy

#endif // PROTOCOLPROFILEACTIONPOLICY_H