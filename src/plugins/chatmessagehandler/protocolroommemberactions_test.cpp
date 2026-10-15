#include <interfaces/iprotocolroommemberactions.h>

#include <iostream>

namespace
{
class ScopedActions final : public IProtocolRoomMemberActions
{
public:
    MemberActions actionsForMember(const AccountId &accountId, const ConversationId &roomId,
        const UserId &memberId) const override
    {
        if (accountId != supportedAccount || roomId != supportedRoom || memberId != supportedMember)
            return MemberActions();
        return supportedActions;
    }

    AccountId supportedAccount = QStringLiteral("persistent-account-A");
    ConversationId supportedRoom = QStringLiteral("!room-A:example.org");
    UserId supportedMember = QStringLiteral("@member-A:example.org");
    MemberActions supportedActions = ActionSetRole | ActionKick | ActionBan;
};

class DeferredActions final : public IProtocolRoomMemberActions
{
public:
    MemberActions observeActionsForMember(const AccountId &accountId, const ConversationId &roomId,
        const UserId &memberId, QObject *context,
        MemberActionsUpdatedCallback callback) override
    {
        if (accountId != supportedAccount || roomId != supportedRoom || memberId != supportedMember)
            return MemberActions();
        FContext = context;
        FCallback = callback;
        if (FCallback)
            FCallback(FCurrentActions);
        return FCurrentActions;
    }

    void resolve(MemberActions actions)
    {
        FCurrentActions = actions;
        if (FContext && FCallback)
            FCallback(FCurrentActions);
    }

    AccountId supportedAccount = QStringLiteral("persistent-account-A");
    ConversationId supportedRoom = QStringLiteral("!room-A:example.org");
    UserId supportedMember = QStringLiteral("@member-A:example.org");
    QObject *FContext = nullptr;
    MemberActions FCurrentActions = ActionOpenDirectConversation;
    MemberActionsUpdatedCallback FCallback;
};

bool check(bool condition, const char *description)
{
    if (!condition)
        std::cerr << "FAIL: " << description << '\n';
    return condition;
}
}

int main()
{
    const AccountId account = QStringLiteral("persistent-account-A");
    const ConversationId room = QStringLiteral("!room-A:example.org");
    const UserId member = QStringLiteral("@member-A:example.org");
    bool passed = true;
    const IProtocolRoomMemberActions unsupported;
    passed &= check(unsupported.actionsForMember(account, room, member) ==
        IProtocolRoomMemberActions::MemberActions(),
        "protocols without member-action support expose no actions by default");

    const ScopedActions actions;
    passed &= check(actions.actionsForMember(account, room, member).testFlag(
        IProtocolRoomMemberActions::ActionKick), "supported room-member operation is exposed");
    passed &= check(actions.actionsForMember(QStringLiteral("another-account"), room, member) ==
        IProtocolRoomMemberActions::MemberActions(),
        "member actions are scoped to the persistent account");
    passed &= check(actions.actionsForMember(account, QStringLiteral("!other-room:example.org"), member) ==
        IProtocolRoomMemberActions::MemberActions(),
        "member actions are scoped to the room");
    passed &= check(actions.actionsForMember(account, room, QStringLiteral("@other:example.org")) ==
        IProtocolRoomMemberActions::MemberActions(),
        "member actions are scoped to the member");

    DeferredActions deferred;
    QObject callbackContext;
    QList<IProtocolRoomMemberActions::MemberActions> snapshots;
    deferred.observeActionsForMember(account, room, member, &callbackContext,
        [&snapshots](IProtocolRoomMemberActions::MemberActions snapshot) {
            snapshots.append(snapshot);
        });
    passed &= check(snapshots.size() == 1 && !snapshots.constFirst().testFlag(
        IProtocolRoomMemberActions::ActionKick),
        "unresolved room permissions initially keep kick hidden");
    deferred.resolve(IProtocolRoomMemberActions::ActionOpenDirectConversation |
        IProtocolRoomMemberActions::ActionKick);
    passed &= check(snapshots.size() == 2 && snapshots.constLast().testFlag(
        IProtocolRoomMemberActions::ActionKick),
        "permission resolution notifies an open profile with the updated kick action");
    return passed ? 0 : 1;
}
