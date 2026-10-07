#include "xmppprofileactionpolicy.h"

namespace XmppProfileActionPolicy
{
IProtocolCapabilities::Capabilities capabilities(bool sessionReady, bool vcardProviderAvailable,
    bool targetValid, bool cachedProfileAvailable)
{
    IProtocolCapabilities::Capabilities result;
    if (!vcardProviderAvailable)
        return result;
    if (sessionReady)
        result |= IProtocolCapabilities::CapabilityEditProfile;
    if (targetValid && (sessionReady || cachedProfileAvailable))
        result |= IProtocolCapabilities::CapabilityShowProfile;
    return result;
}

bool showProfile(IVCardPlugin *vcardPlugin, const Jid &streamJid, const UserId &userId)
{
    const Jid contactJid(userId);
    if (!vcardPlugin || !streamJid.isValid() || !contactJid.isValid())
        return false;
    vcardPlugin->showVCardDialog(streamJid, contactJid.bare());
    return true;
}

bool editProfile(IVCardPlugin *vcardPlugin, const Jid &streamJid)
{
    if (!vcardPlugin || !streamJid.isValid())
        return false;
    vcardPlugin->showVCardDialog(streamJid, streamJid.bare());
    return true;
}
}
