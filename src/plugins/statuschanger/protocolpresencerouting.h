#ifndef PROTOCOLPRESENCEROUTING_H
#define PROTOCOLPRESENCEROUTING_H

#include <interfaces/iprotocolpresence.h>
#include <interfaces/iprotocolcapabilities.h>
#include <interfaces/identity.h>
#include <QList>

namespace ProtocolPresenceRouting
{
inline IProtocolPresence *providerForAccountId(
    const QList<IProtocolPresence *> &providers, const AccountId &accountId)
{
    if (accountId.isEmpty())
        return nullptr;

    for (IProtocolPresence *provider : providers)
        if (provider && provider->accountId() == accountId)
            return provider;

    return nullptr;
}

inline IProtocolPresence *providerForStreamId(
    const QList<IProtocolPresence *> &providers, const QString &streamId)
{
    if (streamId.isEmpty())
        return nullptr;

    for (IProtocolPresence *provider : providers)
        if (provider && provider->streamId() == streamId)
            return provider;

    return nullptr;
}

inline bool canSetPresenceForAccountId(const QList<IProtocolPresence *> &providers,
    const AccountId &accountId)
{
    IProtocolPresence *provider = providerForAccountId(providers, accountId);
    IProtocolCapabilities *capabilities = provider
        ? dynamic_cast<IProtocolCapabilities *>(provider) : nullptr;
    return capabilities && capabilities->hasCapabilities(
        accountId, IProtocolCapabilities::CapabilitySetPresence);
}

inline bool setPresenceForAccountId(const QList<IProtocolPresence *> &providers,
    const AccountId &accountId, int show, const QString &status)
{
    IProtocolPresence *provider = providerForAccountId(providers, accountId);
    IProtocolCapabilities *capabilities = provider
        ? dynamic_cast<IProtocolCapabilities *>(provider) : nullptr;
    return provider && capabilities && capabilities->hasCapabilities(
        accountId, IProtocolCapabilities::CapabilitySetPresence) &&
        provider->setPresence(show, status);
}

inline bool setPresenceForAutoConnect(const QList<IProtocolPresence *> &providers,
    const AccountId &accountId, bool accountActive, bool autoConnectEnabled,
    bool legacyPresenceHandled, int show, const QString &status)
{
    return accountActive && autoConnectEnabled && !legacyPresenceHandled &&
        setPresenceForAccountId(providers, accountId, show, status);
}
}

#endif // PROTOCOLPRESENCEROUTING_H