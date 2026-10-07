#include "protocolaccountavatarpolicy.h"

#include <QList>
#include <iostream>

class AvatarCapabilityProvider : public IProtocolCapabilities
{
public:
    explicit AvatarCapabilityProvider(const AccountId &supportedAccount)
        : FSupportedAccount(supportedAccount) {}

    Capabilities capabilitiesForAccount(const AccountId &accountId,
        const ConversationId &) const override
    {
        return accountId == FSupportedAccount
            ? Capabilities(CapabilitySetAccountAvatar) : Capabilities();
    }

private:
    AccountId FSupportedAccount;
};

class AvatarActionProvider : public IProtocolAccountAvatarActions
{
public:
    bool setAccountAvatar(const AccountId &, const QByteArray &) override { return true; }
};

static bool check(bool condition, const char *message)
{
    if (!condition)
        std::cerr << message << '\n';
    return condition;
}

int main()
{
    const AccountId persistentAccountId = QStringLiteral("matrix-account-uuid");
    const AccountId protocolStreamId = QStringLiteral("alice@matrix.example");
    AvatarCapabilityProvider capabilities(persistentAccountId);
    AvatarActionProvider actions;
    IProtocolCapabilities unsupported;

    bool success = true;
    success &= check(ProtocolAccountAvatarPolicy::canOffer(
        &capabilities, &actions, persistentAccountId),
        "matching account with capability and adapter action should be offered");
    success &= check(!ProtocolAccountAvatarPolicy::canOffer(
        &capabilities, &actions, protocolStreamId),
        "provider stream ID must not substitute for persistent account ID");
    success &= check(!ProtocolAccountAvatarPolicy::canOffer(
        &capabilities, nullptr, persistentAccountId),
        "capability without an adapter operation must remain hidden");
    success &= check(!ProtocolAccountAvatarPolicy::canOffer(
        &unsupported, &actions, persistentAccountId),
        "default unsupported provider must remain hidden");

    QList<ProtocolAccountAvatarPolicy::ProviderIdentity> providerIdentities;
    providerIdentities.append({protocolStreamId, persistentAccountId});
    success &= check(ProtocolAccountAvatarPolicy::accountIdForProtocolStream(
        protocolStreamId, providerIdentities) == persistentAccountId,
        "protocol root stream ID must resolve to the provider's persistent account ID");
    success &= check(ProtocolAccountAvatarPolicy::accountIdForProtocolStream(
        persistentAccountId, providerIdentities).isEmpty(),
        "persistent account ID must not be assumed to equal provider stream ID");
    providerIdentities.append({protocolStreamId, QStringLiteral("other-account-uuid")});
    success &= check(ProtocolAccountAvatarPolicy::accountIdForProtocolStream(
        protocolStreamId, providerIdentities).isEmpty(),
        "ambiguous provider stream IDs must not select an arbitrary account");
    return success ? 0 : 1;
}
