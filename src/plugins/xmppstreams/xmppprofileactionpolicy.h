#ifndef XMPPPROFILEACTIONPOLICY_H
#define XMPPPROFILEACTIONPOLICY_H

#include <interfaces/identity.h>
#include <interfaces/iprotocolcapabilities.h>
#include <interfaces/ivcard.h>

namespace XmppProfileActionPolicy
{
IProtocolCapabilities::Capabilities capabilities(bool sessionReady, bool vcardProviderAvailable,
    bool targetValid, bool cachedProfileAvailable);

bool showProfile(IVCardPlugin *vcardPlugin, const Jid &streamJid, const UserId &userId);
bool editProfile(IVCardPlugin *vcardPlugin, const Jid &streamJid);
}

#endif // XMPPPROFILEACTIONPOLICY_H
