#ifndef PROTOCOLCHATSTATEROUTING_H
#define PROTOCOLCHATSTATEROUTING_H

#include <QList>
#include <interfaces/identity.h>
#include <interfaces/iprotocolpresence.h>

namespace ProtocolChatStateRouting
{
template<typename Providers>
bool sendTyping(const Providers &providers, const AccountId &accountId,
    const ConversationId &conversationId, ProtocolTypingStatus status)
{
    if (accountId.isEmpty() || conversationId.isEmpty())
        return false;

    for (auto *provider : providers)
    {
        if (provider && provider->streamId() == accountId &&
            provider->supportsTyping(conversationId))
        {
            provider->setTyping(conversationId, status);
            return true;
        }
    }
    return false;
}
}

#endif // PROTOCOLCHATSTATEROUTING_H
