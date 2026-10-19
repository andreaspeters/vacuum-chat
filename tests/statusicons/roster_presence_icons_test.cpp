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

    // Test rules() function
    auto userRules = statusIcons.rules(IStatusIcons::UserRule);
    auto defaultRules = statusIcons.rules(IStatusIcons::DefaultRule);
    passed &= check(userRules.isEmpty() && defaultRules.isEmpty(),
        "rules() returns empty lists for both rule types when no rules are set");

    // Test ruleIconset with valid and invalid patterns
    QString userIconset = statusIcons.ruleIconset("pattern1", IStatusIcons::UserRule);
    QString defaultIconset = statusIcons.ruleIconset("pattern2", IStatusIcons::DefaultRule);
    passed &= check(userIconset == STORAGE_SHARED_DIR && defaultIconset == STORAGE_SHARED_DIR,
        "ruleIconset() returns STORAGE_SHARED_DIR for non-existent patterns");

    const IStatusIcons::RuleType invalidRuleType =
        static_cast<IStatusIcons::RuleType>(99);
    const QString invalidRuleIconset =
        statusIcons.ruleIconset("pattern3", invalidRuleType);
    passed &= check(invalidRuleIconset.isEmpty(),
        "ruleIconset() returns empty string for invalid RuleType");
    passed &= check(statusIcons.rules(invalidRuleType).isEmpty(),
        "rules() returns an empty list for invalid RuleType");

    // Test valid insertion and retrieval of rules
    statusIcons.insertRule("user_pattern", "user_storage", IStatusIcons::UserRule);
    statusIcons.insertRule("default_pattern", "default_storage", IStatusIcons::DefaultRule);

    userRules = statusIcons.rules(IStatusIcons::UserRule);
    defaultRules = statusIcons.rules(IStatusIcons::DefaultRule);
    passed &= check(userRules.contains("user_pattern") &&
        !userRules.contains("default_pattern") &&
        defaultRules.contains("default_pattern") &&
        !defaultRules.contains("user_pattern"),
        "rules() keeps user and default patterns isolated");

    statusIcons.insertRule("invalid_pattern", "invalid_storage", invalidRuleType);
    userRules = statusIcons.rules(IStatusIcons::UserRule);
    defaultRules = statusIcons.rules(IStatusIcons::DefaultRule);
    passed &= check(!userRules.contains("invalid_pattern") &&
        !defaultRules.contains("invalid_pattern"),
        "insertRule() ignores invalid RuleType without changing either rule set");

    statusIcons.removeRule("user_pattern", invalidRuleType);
    userRules = statusIcons.rules(IStatusIcons::UserRule);
    passed &= check(userRules.contains("user_pattern"),
        "removeRule() ignores invalid RuleType without removing a valid user rule");

    userIconset = statusIcons.ruleIconset("user_pattern", IStatusIcons::UserRule);
    defaultIconset = statusIcons.ruleIconset("default_pattern", IStatusIcons::DefaultRule);
    passed &= check(userIconset == "user_storage" && defaultIconset == "default_storage",
        "ruleIconset() returns correct storage names for inserted rules");

    // Test removeRule function
    statusIcons.removeRule("user_pattern", IStatusIcons::UserRule);
    userRules = statusIcons.rules(IStatusIcons::UserRule);
    passed &= check(!userRules.contains("user_pattern"),
        "removeRule() removes user rule correctly");

    return passed ? 0 : 1;
}
