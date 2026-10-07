#ifndef PROTOCOLACCOUNTAVATARPOLICY_H
#define PROTOCOLACCOUNTAVATARPOLICY_H

#include <QList>
#include <QString>
#include <interfaces/iprotocolaccountavataractions.h>
#include <interfaces/iprotocolcapabilities.h>

namespace ProtocolAccountAvatarPolicy
{
struct ProviderIdentity
{
    QString streamId;
    AccountId accountId;
};

inline AccountId accountIdForProtocolStream(const QString &streamId,
    const QList<ProviderIdentity> &providers)
{
    if (streamId.isEmpty())
        return AccountId();
    AccountId accountId;
    for (const ProviderIdentity &provider : providers) {
        if (provider.streamId != streamId || provider.accountId.isEmpty())
            continue;
        if (!accountId.isEmpty() && accountId != provider.accountId)
            return AccountId();
        accountId = provider.accountId;
    }
    return accountId;
}

inline bool canOffer(const IProtocolCapabilities *capabilities,
    const IProtocolAccountAvatarActions *actions, const AccountId &accountId)
{
    return capabilities && actions && !accountId.isEmpty() &&
        capabilities->hasCapabilities(accountId,
            IProtocolCapabilities::CapabilitySetAccountAvatar);
}
}

#endif // PROTOCOLACCOUNTAVATARPOLICY_H
