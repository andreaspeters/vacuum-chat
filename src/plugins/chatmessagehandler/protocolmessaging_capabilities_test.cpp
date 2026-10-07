#include <interfaces/iprotocolmessaging.h>
#include <interfaces/iprotocolcapabilities.h>

#include <iostream>

namespace {
class MinimalMessaging final : public IProtocolMessaging
{
public:
    AccountId streamId() const override { return QStringLiteral("test-account"); }
    bool conversationIdForAddress(const Jid &, ConversationId &conversationId) const override
    {
        conversationId = QStringLiteral("test-conversation");
        return true;
    }
    Jid addressForConversation(const ConversationId &) const override { return Jid(); }
    bool sendMessage(const BasicMessage &) override { return false; }
};

bool check(bool condition, const char *description)
{
    if (!condition)
        std::cerr << "FAIL: " << description << '\n';
    return condition;
}

class ScopedCapabilities final : public IProtocolCapabilities
{
public:
    Capabilities capabilitiesForAccount(const AccountId &accountId,
        const ConversationId &targetId) const override
    {
        if (accountId != QStringLiteral("account-1") ||
            (!targetId.isEmpty() && targetId != QStringLiteral("room-1")))
            return CapabilityNone;
        return CapabilityAddContact | CapabilitySetPresence;
    }
};
}

int main()
{
    MinimalMessaging messaging;
    bool passed = true;
    const ConversationId conversationId = QStringLiteral("test-conversation");
    passed &= check(!messaging.supportsReplies(conversationId),
        "reply support defaults to disabled");
    passed &= check(!messaging.supportsReactions(conversationId),
        "reaction support defaults to disabled");
    passed &= check(!messaging.providesAvatarUpdateSignals(),
        "avatar update signal capability defaults to disabled");
    passed &= check(!messaging.providesHistoryLoadedSignals(),
        "history loaded signal capability defaults to disabled");
    passed &= check(!messaging.supportsConversationMedia(),
        "conversation media capability defaults to disabled");
    passed &= check(!messaging.supportsFileTransfer(),
        "file transfer capability defaults to disabled");
    passed &= check(!messaging.sendReaction(conversationId, QStringLiteral("message-id"),
        QStringLiteral("👍")), "unsupported reactions fail safely");
    BasicMessage historyCursor;
    bool historyPageCallbackCalled = false;
    passed &= check(!messaging.supportsOlderHistory(conversationId),
        "older-history paging defaults to unsupported");
    passed &= check(!messaging.requestOlderHistoryPage(conversationId, historyCursor, 30, nullptr,
        [&historyPageCallbackCalled](ProtocolHistoryPage) { historyPageCallbackCalled = true; }),
        "unsupported older-history request is rejected safely");
    passed &= check(!historyPageCallbackCalled,
        "unsupported older-history request does not invoke its callback");

    IProtocolCapabilities unsupported;
    passed &= check(!unsupported.hasCapabilities(QStringLiteral("account-1"),
        IProtocolCapabilities::CapabilityAddContact),
        "protocol capability defaults to unsupported");

    ScopedCapabilities scoped;
    const auto addContact = IProtocolCapabilities::CapabilityAddContact;
    const auto setPresence = IProtocolCapabilities::CapabilitySetPresence;
    const auto editProfile = IProtocolCapabilities::CapabilityEditProfile;
    passed &= check(scoped.hasCapabilities(QStringLiteral("account-1"), addContact),
        "supported capability is available for the matching account");
    passed &= check(scoped.hasCapabilities(QStringLiteral("account-1"), addContact | setPresence,
        QStringLiteral("room-1")), "combined request requires and receives every requested flag");
    passed &= check(!scoped.hasCapabilities(QStringLiteral("account-1"), addContact | editProfile),
        "combined request fails if any requested flag is unsupported");
    passed &= check(!scoped.hasCapabilities(QStringLiteral("account-2"), addContact),
        "capabilities are scoped to the account identity");
    passed &= check(!scoped.hasCapabilities(QStringLiteral("account-1"), addContact,
        QStringLiteral("room-2")), "target-scoped capability checks reject other targets");
    passed &= check(!scoped.hasCapabilities(QStringLiteral("account-1"),
        IProtocolCapabilities::CapabilityNone), "empty capability request is false");
    return passed ? 0 : 1;
}
