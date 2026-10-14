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
    return passed ? 0 : 1;
}
