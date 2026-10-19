#include "xmppstreams.h"

#include <QCoreApplication>
#include <interfaces/iprotocolprofileactions.h>
#include <iostream>

namespace
{
bool check(bool condition, const char *message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    XmppStreams adapter;
    bool passed = true;
    IProtocolProfileActions *actions = qobject_cast<IProtocolProfileActions *>(adapter.instance());
    passed &= check(actions != NULL, "XMPP streams exposes the generic profile-action interface");
    const AccountId accountId = QStringLiteral("11111111-2222-4333-8444-555555555555");
    const UserId userId = QStringLiteral("bob@example.test");
    const IProtocolCapabilities::Capabilities capabilities = adapter.capabilitiesForAccount(accountId, userId);
    passed &= check(!capabilities.testFlag(IProtocolCapabilities::CapabilityShowProfile) &&
        !capabilities.testFlag(IProtocolCapabilities::CapabilityEditProfile),
        "profile actions are unavailable without a bound XMPP account and VCard provider");
    if (actions) {
        passed &= check(!actions->showProfile(accountId, userId),
            "Show Profile safely rejects an unbound account");
        passed &= check(!actions->editProfile(accountId),
            "Edit Profile safely rejects an unbound account");
    }
    return passed ? 0 : 1;
}
