#include "rostersmodel.h"

#include <QCoreApplication>
#include <iostream>

class RostersModelPresenceTestAccess
{
public:
    static void bindPresenceProvider(RostersModel &model, IProtocolPresence *presence)
    {
        model.bindProtocolPresenceProvider(presence);
    }

    static void bindRosterProvider(RostersModel &model, IProtocolRoster *roster)
    {
        model.FProtocolRosters.append(roster);
    }

    static void rebuild(RostersModel &model, IProtocolRoster *roster)
    {
        model.rebuildProtocolRoster(roster);
    }
};

namespace
{
class FakePresence final : public QObject, public IProtocolPresence
{
    Q_OBJECT
    Q_INTERFACES(IProtocolPresence)
public:
    QString id = QStringLiteral("@user:example.org");
    int currentShow = IPresence::Offline;
    QString currentStatus = QStringLiteral("Offline");

    QObject *instance() override { return this; }
    QString streamId() const override { return id; }
    AccountId accountId() const override { return QStringLiteral("stable-account-id"); }
    int show() const override { return currentShow; }
    QString status() const override { return currentStatus; }
    bool setPresence(int showValue, const QString &statusValue) override
    {
        currentShow = showValue;
        currentStatus = statusValue;
        return true;
    }

    void publishPresence(int showValue, const QString &statusValue)
    {
        currentShow = showValue;
        currentStatus = statusValue;
        emit protocolPresenceChanged(id, showValue, statusValue);
    }

signals:
    void protocolPresenceChanged(const QString &streamId, int show, const QString &status);
    void protocolPresenceClosed(const QString &streamId);
};

class FakeRoster final : public IProtocolRoster
{
public:
    QString accountId() const override { return QStringLiteral("stable-account-id"); }
    QString protocol() const override { return QStringLiteral("fixture"); }
    QString streamId() const override { return QStringLiteral("@user:example.org"); }
    QString accountAvatarKey() const override { return QStringLiteral("matrix-self-avatar-key"); }
    QList<ProtocolRosterEntry> entries() const override { return {}; }
    QList<ProtocolRoom> rooms() const override
    {
        ProtocolRoom room;
        room.id = QStringLiteral("!room:example.org");
        room.name = QStringLiteral("Fixture room");
        room.isJoined = true;
        return {room};
    }
    ProtocolRosterEntry entry(const QString &) const override { return {}; }
    ProtocolRoom room(const QString &) const override { return {}; }
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
    RostersModel model;
    FakePresence presence;
    FakeRoster roster;
    RostersModelPresenceTestAccess::bindPresenceProvider(model, &presence);
    RostersModelPresenceTestAccess::bindRosterProvider(model, &roster);
    RostersModelPresenceTestAccess::rebuild(model, &roster);

    IRosterIndex *root = model.protocolStreamRoot(roster.streamId());
    IRosterIndex *room = root && root->childCount() >= 2
        ? root->child(1)->child(0) : nullptr;
    bool passed = true;
    passed &= check(root != nullptr, "protocol account root is created");
    passed &= check(room != nullptr, "protocol room row is created");
    if (!root || !room)
        return 1;

    passed &= check(root->data(RDR_AVATAR_KEY).toString() ==
            QStringLiteral("matrix-self-avatar-key"),
        "protocol account root exposes the provider's own avatar key");
    passed &= check(root->data(RDR_SHOW).toInt() == IPresence::Offline,
        "new protocol account root reflects the provider's offline state");
    passed &= check(room->data(RDR_SHOW).toInt() == IPresence::Offline,
        "rebuilt protocol room row reflects the provider's offline state");

    presence.publishPresence(IPresence::Online, QStringLiteral("Online"));
    passed &= check(root->data(RDR_SHOW).toInt() == IPresence::Online,
        "online provider state updates the account lamp role");
    passed &= check(room->data(RDR_SHOW).toInt() == IPresence::Online,
        "online provider state updates existing room rows");

    presence.publishPresence(IPresence::Offline, QStringLiteral("Offline"));
    passed &= check(root->data(RDR_SHOW).toInt() == IPresence::Offline,
        "offline provider state updates the account lamp role");
    passed &= check(room->data(RDR_SHOW).toInt() == IPresence::Offline,
        "offline provider state updates existing room rows");

    return passed ? 0 : 1;
}

#include "roster_protocol_presence_test.moc"
