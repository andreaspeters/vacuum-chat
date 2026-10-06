#include <interfaces/iprotocolmessaging.h>

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
    return passed ? 0 : 1;
}
