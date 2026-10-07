#ifndef PROTOCOLACCOUNTROOTRESOLVER_H
#define PROTOCOLACCOUNTROOTRESOLVER_H

#include <interfaces/identity.h>
#include <interfaces/iprotocolroster.h>

#include <QList>
#include <QString>

namespace ProtocolAccountRootResolver
{
inline AccountId persistentAccountIdForStreamId(const QString &streamId,
    const QList<IProtocolRoster *> &providers)
{
    if (streamId.isEmpty())
        return AccountId();

    AccountId resolvedAccountId;
    for (IProtocolRoster *provider : providers)
    {
        if (!provider || provider->streamId() != streamId)
            continue;

        const AccountId candidateAccountId = provider->accountId();
        if (candidateAccountId.isEmpty())
            return AccountId();
        if (!resolvedAccountId.isEmpty() && resolvedAccountId != candidateAccountId)
            return AccountId();
        resolvedAccountId = candidateAccountId;
    }
    return resolvedAccountId;
}
}

#endif // PROTOCOLACCOUNTROOTRESOLVER_H
