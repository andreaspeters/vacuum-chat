#ifndef XMPPHISTORYCAPABILITYPOLICY_H
#define XMPPHISTORYCAPABILITYPOLICY_H

#include <interfaces/iprotocolcapabilities.h>

namespace XmppHistoryCapabilityPolicy
{
inline IProtocolCapabilities::Capabilities capabilities(bool accountConfigured,
    bool localProviderAvailable, bool conversationIdValid)
{
    return accountConfigured && localProviderAvailable && conversationIdValid
        ? IProtocolCapabilities::Capabilities(IProtocolCapabilities::CapabilityViewHistory)
        : IProtocolCapabilities::Capabilities(IProtocolCapabilities::CapabilityNone);
}
}

#endif // XMPPHISTORYCAPABILITYPOLICY_H
