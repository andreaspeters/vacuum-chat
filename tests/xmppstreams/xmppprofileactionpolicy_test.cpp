#include "xmppprofileactionpolicy.h"

#include <QCoreApplication>
#include <QObject>
#include <interfaces/identity.h>
#include <interfaces/ivcard.h>

#include <iostream>

namespace
{
class TestVCardPlugin : public QObject, public IVCardPlugin
{
public:
    QObject *instance() override { return this; }
    QString vcardFileName(const Jid &) const override { return QString(); }
    bool hasVCard(const Jid &) const override { return false; }
    bool requestVCard(const Jid &, const Jid &) override { return false; }
    IVCard *vcard(const Jid &) override { return NULL; }
    bool publishVCard(IVCard *, const Jid &) override { return false; }
    void showVCardDialog(const Jid &streamJid, const Jid &contactJid) override
    {
        ++showCalls;
        lastStream = streamJid;
        lastContact = contactJid;
    }

    int showCalls = 0;
    Jid lastStream;
    Jid lastContact;

protected:
    void vcardReceived(const Jid &) override {}
    void vcardPublished(const Jid &) override {}
    void vcardError(const Jid &, const XmppError &) override {}
};

bool check(bool condition, const char *message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

bool testProfileCapabilities()
{
    using namespace XmppProfileActionPolicy;
    bool passed = true;
    const auto unavailable = capabilities(false, false, true, true);
    passed &= check(!unavailable.testFlag(IProtocolCapabilities::CapabilityShowProfile) &&
        !unavailable.testFlag(IProtocolCapabilities::CapabilityEditProfile),
        "profile operations require the VCard provider");

    const auto connected = capabilities(true, true, true, false);
    passed &= check(connected.testFlag(IProtocolCapabilities::CapabilityShowProfile) &&
        connected.testFlag(IProtocolCapabilities::CapabilityEditProfile),
        "connected XMPP with VCard supports show and edit");

    const auto cachedOffline = capabilities(false, true, true, true);
    passed &= check(cachedOffline.testFlag(IProtocolCapabilities::CapabilityShowProfile) &&
        !cachedOffline.testFlag(IProtocolCapabilities::CapabilityEditProfile),
        "offline cached VCards remain viewable but not editable");

    const auto uncachedOffline = capabilities(false, true, true, false);
    passed &= check(!uncachedOffline.testFlag(IProtocolCapabilities::CapabilityShowProfile) &&
        !uncachedOffline.testFlag(IProtocolCapabilities::CapabilityEditProfile),
        "offline uncached profiles expose no action");

    const auto noTarget = capabilities(true, true, false, false);
    passed &= check(!noTarget.testFlag(IProtocolCapabilities::CapabilityShowProfile) &&
        noTarget.testFlag(IProtocolCapabilities::CapabilityEditProfile),
        "an empty target cannot expose Show Profile");
    return passed;
}

bool testVCardDispatch()
{
    bool passed = true;
    TestVCardPlugin vcard;
    const Jid streamJid(QStringLiteral("alice@example.test/desktop"));
    const UserId userId = QStringLiteral("bob@example.test/mobile");

    passed &= check(XmppProfileActionPolicy::showProfile(&vcard, streamJid, userId),
        "valid Show Profile request is accepted");
    passed &= check(vcard.showCalls == 1 && vcard.lastStream == streamJid &&
        vcard.lastContact == Jid(QStringLiteral("bob@example.test")),
        "Show Profile opens the VCard for the bare target JID");

    passed &= check(XmppProfileActionPolicy::editProfile(&vcard, streamJid),
        "valid Edit Profile request is accepted");
    passed &= check(vcard.showCalls == 2 && vcard.lastStream == streamJid &&
        vcard.lastContact == streamJid.bare(),
        "Edit Profile opens the account owner's VCard");

    passed &= check(!XmppProfileActionPolicy::showProfile(&vcard, streamJid, UserId()),
        "an empty target is rejected");
    passed &= check(!XmppProfileActionPolicy::editProfile(NULL, streamJid),
        "missing VCard provider is rejected");
    passed &= check(vcard.showCalls == 2,
        "rejected requests do not open extra dialogs");
    return passed;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    return testProfileCapabilities() && testVCardDispatch() ? 0 : 1;
}
