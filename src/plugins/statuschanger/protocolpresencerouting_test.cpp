#include "protocolpresencerouting.h"

#include <interfaces/iprotocolcapabilities.h>
#include <iostream>

namespace
{
class MockProtocolPresence final : public IProtocolPresence, public IProtocolCapabilities
{
public:
    MockProtocolPresence(const AccountId &stableId, const QString &stream, int value)
        : persistentId(stableId), protocolStreamId(stream), currentShow(value), enabled(true) {}

    QObject *instance() override { return nullptr; }
    QString streamId() const override { return protocolStreamId; }
    AccountId accountId() const override { return persistentId; }
    int show() const override { return currentShow; }
    QString status() const override { return QString(); }
    bool setPresence(int value, const QString &) override
    {
        currentShow = value;
        return true;
    }
    Capabilities capabilitiesForAccount(const AccountId &id,
        const ConversationId &targetId = ConversationId()) const override
    {
        Q_UNUSED(targetId);
        return enabled && id == persistentId
            ? Capabilities(CapabilitySetPresence) : Capabilities();
    }

    AccountId persistentId;
    QString protocolStreamId;
    int currentShow;
    bool enabled;
};

bool check(bool condition, const char *message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}
}

int main()
{
    MockProtocolPresence matrix(QStringLiteral("matrix-account-uuid"),
        QStringLiteral("@alice:example.org"), 1);
    MockProtocolPresence another(QStringLiteral("other-account-uuid"),
        QStringLiteral("@bob:example.org"), 2);
    const QList<IProtocolPresence *> providers{&matrix, &another};
    bool passed = true;

    passed &= check(ProtocolPresenceRouting::providerForAccountId(providers,
        QStringLiteral("other-account-uuid")) == &another,
        "provider lookup uses stable account ID, not protocol stream ID");
    passed &= check(ProtocolPresenceRouting::providerForAccountId(providers,
        QStringLiteral("@bob:example.org")) == nullptr,
        "protocol stream ID is not accepted as persistent account ID");
    passed &= check(ProtocolPresenceRouting::providerForAccountId(providers,
        QStringLiteral("missing-account-uuid")) == nullptr,
        "unknown stable account ID has no provider");

    passed &= check(ProtocolPresenceRouting::setPresenceForAccountId(providers,
        QStringLiteral("other-account-uuid"), 3, QStringLiteral("busy")),
        "status update reaches the matching account");
    passed &= check(another.show() == 3 && matrix.show() == 1,
        "account-scoped status update does not affect another provider");

    another.enabled = false;
    passed &= check(!ProtocolPresenceRouting::setPresenceForAccountId(providers,
        QStringLiteral("other-account-uuid"), 4, QStringLiteral("offline")),
        "dispatch rechecks live SetPresence capability");
    passed &= check(another.show() == 3,
        "unsupported or stale capability does not mutate presence");

    passed &= check(ProtocolPresenceRouting::setPresenceForAutoConnect(providers,
        QStringLiteral("matrix-account-uuid"), true, true, false, 7,
        QStringLiteral("startup")),
        "enabled generic startup account dispatches by persistent account ID");
    passed &= check(matrix.show() == 7 && another.show() == 3,
        "startup dispatch updates only the matching protocol provider");
    passed &= check(!ProtocolPresenceRouting::setPresenceForAutoConnect(providers,
        QStringLiteral("matrix-account-uuid"), false, true, false, 8,
        QStringLiteral("inactive")) && matrix.show() == 7,
        "inactive accounts do not auto-connect");
    passed &= check(!ProtocolPresenceRouting::setPresenceForAutoConnect(providers,
        QStringLiteral("matrix-account-uuid"), true, false, false, 8,
        QStringLiteral("disabled")) && matrix.show() == 7,
        "accounts without Auto connect on startup do not dispatch");
    passed &= check(!ProtocolPresenceRouting::setPresenceForAutoConnect(providers,
        QStringLiteral("matrix-account-uuid"), true, true, true, 8,
        QStringLiteral("legacy")) && matrix.show() == 7,
        "accounts handled by the legacy XMPP path are not dispatched twice");
    return passed ? 0 : 1;
}
