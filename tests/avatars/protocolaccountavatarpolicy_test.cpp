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
    AccountId receivedAccount;
    QByteArray receivedData;
    int calls = 0;

    bool setAccountAvatar(const AccountId &accountId, const QByteArray &imageData) override
    {
        ++calls;
        receivedAccount = accountId;
        receivedData = imageData;
        return true;
    }
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

    AvatarActionProvider dispatchProvider;
    const QByteArray avatarData("synthetic-image-data");
    success &= check(ProtocolAccountAvatarPolicy::dispatchSetAvatar(
        &dispatchProvider, persistentAccountId, avatarData),
        "avatar dispatch should invoke the matching adapter action");
    success &= check(dispatchProvider.calls == 1 &&
        dispatchProvider.receivedAccount == persistentAccountId &&
        dispatchProvider.receivedData == avatarData,
        "avatar dispatch must preserve the account ID and image bytes");
    success &= check(!ProtocolAccountAvatarPolicy::dispatchSetAvatar(
        nullptr, persistentAccountId, avatarData) && dispatchProvider.calls == 1,
        "missing adapter action must not dispatch");
    success &= check(!ProtocolAccountAvatarPolicy::dispatchSetAvatar(
        &dispatchProvider, AccountId(), avatarData) && dispatchProvider.calls == 1,
        "empty account ID must not dispatch");

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
