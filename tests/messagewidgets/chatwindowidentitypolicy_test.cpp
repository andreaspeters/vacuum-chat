#include "chatwindowidentitypolicy.h"

#include <iostream>

namespace
{
bool check(bool condition, const char *description)
{
    if (!condition)
        std::cerr << "FAIL: " << description << '\n';
    return condition;
}
}

int main()
{
    const AccountId accountId = QStringLiteral("{a25b74b7-4146-45fc-9279-1de2cf4ea510}");
    const Jid bareJid(QStringLiteral("alice@example.org"));
    const Jid fullJid(QStringLiteral("alice@example.org/phone"));
    bool passed = true;

    const ChatWindowIdentityPolicy::Identity bare =
        ChatWindowIdentityPolicy::fromXmppJids(accountId, bareJid);
    passed &= check(bare.isValid(), "valid XMPP account and address produce a window identity");
    passed &= check(bare.accountId == accountId,
        "identity preserves the persistent account identifier");
    passed &= check(bare.conversationId == bareJid.pFull(),
        "bare XMPP address remains the conversation identity");

    const ChatWindowIdentityPolicy::Identity full =
        ChatWindowIdentityPolicy::fromXmppJids(accountId, fullJid);
    passed &= check(full.isValid() && full.conversationId == fullJid.pFull(),
        "resource-qualified XMPP chat keeps its exact legacy conversation key");
    passed &= check(full.conversationId != bare.conversationId,
        "resource-qualified private chats remain distinct from the bare address");

    passed &= check(!ChatWindowIdentityPolicy::fromXmppJids(AccountId(), bareJid).isValid(),
        "missing persistent account identity is rejected");
    passed &= check(!ChatWindowIdentityPolicy::fromXmppJids(accountId, Jid()).isValid(),
        "invalid XMPP address is rejected");
    return passed ? 0 : 1;
}
