#ifndef IPROTOCOLMESSAGINGREGISTRY_H
#define IPROTOCOLMESSAGINGREGISTRY_H

#include <interfaces/iprotocolmessaging.h>

// Resolves a protocol-neutral messaging provider for a persistent account ID.
// The returned provider is owned by the registry and remains valid while the
// registry is alive.
class IProtocolMessagingRegistry
{
public:
    virtual ~IProtocolMessagingRegistry() {}
    virtual IProtocolMessaging *providerForAccount(const AccountId &accountId) = 0;
};

Q_DECLARE_INTERFACE(IProtocolMessagingRegistry, "Vacuum.Plugin.IProtocolMessagingRegistry/1.0")

#endif // IPROTOCOLMESSAGINGREGISTRY_H
