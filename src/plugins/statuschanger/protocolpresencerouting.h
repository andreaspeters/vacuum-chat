#ifndef PROTOCOLPRESENCEROUTING_H
#define PROTOCOLPRESENCEROUTING_H

#include <interfaces/iprotocolpresence.h>
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
        if (provider && provider->streamId() == accountId)
            return provider;

    return nullptr;
}

inline bool setPresenceForAccountId(const QList<IProtocolPresence *> &providers,
    const AccountId &accountId, int show, const QString &status)
{
    IProtocolPresence *provider = providerForAccountId(providers, accountId);
    return provider && provider->setPresence(show, status);
}
}

#endif // PROTOCOLPRESENCEROUTING_H
