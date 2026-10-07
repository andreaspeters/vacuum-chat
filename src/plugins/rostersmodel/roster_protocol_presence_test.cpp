#include "rostersmodel.h"

#include <QCoreApplication>
#include <iostream>

class RostersModelPresenceTestAccess
{
public:
    static void addPresenceProvider(RostersModel &model, IProtocolPresence *presence)
    {
        model.FProtocolPresences.append(presence);
    }

    static void rebuild(RostersModel &model, IProtocolRoster *roster)
    {
        model.rebuildProtocolRoster(roster);
    }

    static void presenceChanged(RostersModel &model, const QString &streamId,
        int show, const QString &status)
    {
        model.onProtocolPresenceChanged(streamId, show, status);
    }
};

namespace
{
class FakePresence final : public QObject, public IProtocolPresence
{
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
};

class FakeRoster final : public IProtocolRoster
{
public:
    QString accountId() const override { return QStringLiteral("stable-account-id"); }
    QString protocol() const override { return QStringLiteral("fixture"); }
    QString streamId() const override { return QStringLiteral("@user:example.org"); }
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
    RostersModelPresenceTestAccess::addPresenceProvider(model, &presence);
    RostersModelPresenceTestAccess::rebuild(model, &roster);

    IRosterIndex *root = model.protocolStreamRoot(roster.streamId());
    IRosterIndex *room = root && root->childCount() >= 2
        ? root->child(1)->child(0) : nullptr;
    bool passed = true;
    passed &= check(root != nullptr, "protocol account root is created");
    passed &= check(room != nullptr, "protocol room row is created");
    if (!root || !room)
        return 1;

    passed &= check(root->data(RDR_SHOW).toInt() == IPresence::Offline,
        "new protocol account root reflects the provider's offline state");
    passed &= check(room->data(RDR_SHOW).toInt() == IPresence::Offline,
        "rebuilt protocol room row reflects the provider's offline state");

    presence.currentShow = IPresence::Online;
    presence.currentStatus = QStringLiteral("Online");
    RostersModelPresenceTestAccess::presenceChanged(model, roster.streamId(),
        IPresence::Online, QStringLiteral("Online"));
    passed &= check(root->data(RDR_SHOW).toInt() == IPresence::Online,
        "online provider state updates the account lamp role");
    passed &= check(room->data(RDR_SHOW).toInt() == IPresence::Online,
        "online provider state updates existing room rows");

    presence.currentShow = IPresence::Offline;
    presence.currentStatus = QStringLiteral("Offline");
    RostersModelPresenceTestAccess::presenceChanged(model, roster.streamId(),
        IPresence::Offline, QStringLiteral("Offline"));
    passed &= check(root->data(RDR_SHOW).toInt() == IPresence::Offline,
        "offline provider state updates the account lamp role");
    passed &= check(room->data(RDR_SHOW).toInt() == IPresence::Offline,
        "offline provider state updates existing room rows");

    return passed ? 0 : 1;
}
