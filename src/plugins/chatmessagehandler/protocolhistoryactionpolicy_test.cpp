#include <interfaces/identity.h>
#include <interfaces/iprotocolcapabilities.h>
#include <interfaces/protocolhistoryactionpolicy.h>

#include <iostream>

namespace {
class FixedCapabilities final : public IProtocolCapabilities
{
public:
    FixedCapabilities(const AccountId &accountId, const ConversationId &conversationId,
        Capabilities capabilities)
        : supportedAccount(accountId), supportedConversation(conversationId), flags(capabilities)
    {
    }

    Capabilities capabilitiesForAccount(const AccountId &accountId,
        const ConversationId &conversationId) const override
    {
        if (accountId != supportedAccount ||
            (!supportedConversation.isEmpty() && conversationId != supportedConversation))
            return CapabilityNone;
        return flags;
    }

private:
    AccountId supportedAccount;
    ConversationId supportedConversation;
    Capabilities flags;
};

bool check(bool condition, const char *description)
{
    if (!condition)
        std::cerr << "FAIL: " << description << '\n';
    return condition;
}
}

int main()
{
    const AccountId accountId = QStringLiteral("persistent-account-17");
    const QString providerStreamId = QStringLiteral("matrix-stream-@alice:example.org");
    const ConversationId conversationId = QStringLiteral("!room:example.org");
    const auto viewHistory = IProtocolCapabilities::CapabilityViewHistory;
    const auto manageArchive = IProtocolCapabilities::CapabilityManageRemoteArchive;
    bool passed = true;

    const FixedCapabilities localOnly(accountId, conversationId, viewHistory);
    passed &= check(ProtocolHistoryActionPolicy::canViewLocalConversationHistory(
        &localOnly, accountId, providerStreamId, providerStreamId, conversationId),
        "local-history capability offers local history for the matching provider stream");
    passed &= check(!ProtocolHistoryActionPolicy::canManageRemoteArchive(
        &localOnly, accountId), "local-history support does not offer remote archive management");

    const FixedCapabilities remoteOnly(accountId, QString(), manageArchive);
    passed &= check(!ProtocolHistoryActionPolicy::canViewLocalConversationHistory(
        &remoteOnly, accountId, providerStreamId, providerStreamId, conversationId),
        "remote-archive support does not offer local history");
    passed &= check(ProtocolHistoryActionPolicy::canManageRemoteArchive(
        &remoteOnly, accountId), "remote-archive capability offers remote archive management");

    passed &= check(!ProtocolHistoryActionPolicy::canViewLocalConversationHistory(
        &localOnly, accountId, providerStreamId, providerStreamId, QString()),
        "local-history action requires a conversation ID");
    passed &= check(!ProtocolHistoryActionPolicy::canViewLocalConversationHistory(
        &localOnly, accountId, providerStreamId, QStringLiteral("other-provider-stream"),
        conversationId), "local history is not routed to a different provider stream");
    passed &= check(!ProtocolHistoryActionPolicy::canManageRemoteArchive(
        &remoteOnly, QStringLiteral("another-account")), "remote archive capability is account-scoped");
    return passed ? 0 : 1;
}
