#ifndef CHATWINDOWIDENTITYPOLICY_H
#define CHATWINDOWIDENTITYPOLICY_H

#include <interfaces/identity.h>
#include <utils/jid.h>

namespace ChatWindowIdentityPolicy
{
struct Identity
{
    AccountId accountId;
    ConversationId conversationId;

    bool isValid() const
    {
        return !accountId.isEmpty() && !conversationId.isEmpty();
    }
};

inline Identity fromXmppJids(const AccountId &accountId, const Jid &contactJid)
{
    if (accountId.isEmpty() || !contactJid.isValid())
        return Identity();
    return Identity{accountId, contactJid.pFull()};
}
}

#endif // CHATWINDOWIDENTITYPOLICY_H
