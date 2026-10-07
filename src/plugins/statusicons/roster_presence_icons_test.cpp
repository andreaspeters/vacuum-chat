#include "statusicons.h"
#include "rosterindex.h"

#include <QCoreApplication>
#include <iostream>

namespace
{
enum IconSource
{
    NoIconSource,
    StatusIconSource,
    JidIconSource,
    IdentityIconSource
};

class ProbeStatusIcons : public StatusIcons
{
public:
    mutable IconSource source = NoIconSource;
    mutable int selectedShow = -1;

    QIcon iconByJid(const Jid &, const Jid &) const override
    {
        source = JidIconSource;
        return QIcon();
    }

    QIcon iconByIdentity(const AccountId &, const UserId &) const override
    {
        source = IdentityIconSource;
        return QIcon();
    }

    QIcon iconByStatus(int show, const QString &, bool) const override
    {
        source = StatusIconSource;
        selectedShow = show;
        return QIcon();
    }

    void resetProbe() const
    {
        source = NoIconSource;
        selectedShow = -1;
    }
};

bool check(bool condition, const char *message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    ProbeStatusIcons statusIcons;
    bool passed = true;

    RosterIndex protocolRoot(RIT_STREAM_ROOT);
    protocolRoot.setData(RDR_ACCOUNT_ID, QStringLiteral("account-uuid-a"));
    protocolRoot.setData(RDR_SHOW, IPresence::Online);
    statusIcons.rosterData(&protocolRoot, Qt::DecorationRole);
    passed &= check(statusIcons.source == StatusIconSource &&
        statusIcons.selectedShow == IPresence::Online,
        "protocol account root selects its current Online status icon");

    statusIcons.resetProbe();
    protocolRoot.setData(RDR_SHOW, IPresence::Offline);
    statusIcons.rosterData(&protocolRoot, Qt::DecorationRole);
    passed &= check(statusIcons.source == StatusIconSource &&
        statusIcons.selectedShow == IPresence::Offline,
        "protocol account root selects the dedicated Offline status icon");
    passed &= check(statusIcons.iconKeyByStatus(IPresence::Offline,
        SUBSCRIPTION_BOTH, false) == STI_OFFLINE,
        "Offline presence resolves to the existing Offline icon variant");

    RosterIndex protocolContact(RIT_CONTACT);
    protocolContact.setData(RDR_ACCOUNT_ID, QStringLiteral("account-uuid-a"));
    protocolContact.setData(RDR_CONVERSATION_ID, QStringLiteral("!room-42:example.org"));
    protocolContact.setData(RDR_SHOW, IPresence::Offline);
    statusIcons.resetProbe();
    statusIcons.rosterData(&protocolContact, Qt::DecorationRole);
    passed &= check(statusIcons.source == StatusIconSource &&
        statusIcons.selectedShow == IPresence::Offline,
        "protocol identity row selects its current Offline status icon");

    RosterIndex legacyContact(RIT_CONTACT);
    legacyContact.setData(RDR_STREAM_JID, QStringLiteral("alice@example.org"));
    legacyContact.setData(RDR_FULL_JID, QStringLiteral("bob@example.org"));
    legacyContact.setData(RDR_SHOW, IPresence::Offline);
    statusIcons.resetProbe();
    statusIcons.rosterData(&legacyContact, Qt::DecorationRole);
    passed &= check(statusIcons.source == JidIconSource,
        "legacy JID roster entries keep their JID-based icon selection");

    return passed ? 0 : 1;
}
