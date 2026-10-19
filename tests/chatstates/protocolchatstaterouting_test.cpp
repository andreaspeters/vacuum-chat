#include "protocolchatstaterouting.h"

#include <iostream>
#include <QList>

namespace {
struct FakeMessaging
{
    AccountId account;
    ConversationId conversation;
    bool typingSupported;
    int sendCount;
    ConversationId sentConversation;
    ProtocolTypingStatus sentStatus;

    FakeMessaging(const AccountId &accountId, const ConversationId &conversationId, bool supported)
        : account(accountId), conversation(conversationId), typingSupported(supported), sendCount(0),
          sentStatus(TypingStatusNotTyping)
    {
    }

    AccountId streamId() const { return account; }
    bool supportsTyping(const ConversationId &conversationId) const
    { return typingSupported && conversationId == conversation; }
    void setTyping(const ConversationId &conversationId, ProtocolTypingStatus status)
    {
        ++sendCount;
        sentConversation = conversationId;
        sentStatus = status;
    }
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
    FakeMessaging unrelatedAccount(QStringLiteral("account-a"), QStringLiteral("!room"), true);
    FakeMessaging unsupported(QStringLiteral("account-b"), QStringLiteral("!room"), false);
    FakeMessaging matching(QStringLiteral("account-b"), QStringLiteral("!room"), true);
    QList<FakeMessaging *> providers;
    providers << &unrelatedAccount << &unsupported << &matching;

    bool passed = true;
    const bool dispatched = ProtocolChatStateRouting::sendTyping(providers,
        AccountId(QStringLiteral("account-b")), ConversationId(QStringLiteral("!room")),
        TypingStatusComposing);
    passed &= check(dispatched, "matching provider accepts a typing update");
    passed &= check(unrelatedAccount.sendCount == 0 && unsupported.sendCount == 0 &&
        matching.sendCount == 1, "only the exact supported provider receives the update");
    passed &= check(matching.sentConversation == QStringLiteral("!room") &&
        matching.sentStatus == TypingStatusComposing, "conversation and typing status are preserved");

    const bool stopped = ProtocolChatStateRouting::sendTyping(providers,
        AccountId(QStringLiteral("account-b")), ConversationId(QStringLiteral("!room")),
        TypingStatusNotTyping);
    passed &= check(stopped && matching.sendCount == 2 && matching.sentStatus == TypingStatusNotTyping,
        "typing stop is routed to the exact provider");

    passed &= check(!ProtocolChatStateRouting::sendTyping(providers,
        AccountId(QStringLiteral("missing-account")), ConversationId(QStringLiteral("!room")),
        TypingStatusNotTyping), "missing account does not fall back to another provider");
    passed &= check(!ProtocolChatStateRouting::sendTyping(providers,
        AccountId(QStringLiteral("account-b")), ConversationId(), TypingStatusNotTyping),
        "empty conversation is rejected");
    passed &= check(matching.sendCount == 2, "rejected updates are not dispatched");

    QList<FakeMessaging *> unsupportedProviders;
    unsupportedProviders << &unsupported;
    passed &= check(!ProtocolChatStateRouting::sendTyping(unsupportedProviders,
        AccountId(QStringLiteral("account-b")), ConversationId(QStringLiteral("!room")),
        TypingStatusComposing), "a provider without typing capability is not called");
    passed &= check(unsupported.sendCount == 0, "unsupported provider receives no typing update");

    return passed ? 0 : 1;
}
