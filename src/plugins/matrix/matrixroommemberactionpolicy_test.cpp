#include "matrixroommemberactionpolicy.h"

#include <QJsonObject>

#include <iostream>

namespace
{
bool expect(bool condition, const char *description)
{
    if (!condition)
        std::cerr << "FAIL: " << description << '\n';
    return condition;
}
}

int main()
{
    ProtocolRoom room;
    room.id = QStringLiteral("!room-A:example.org");
    room.isJoined = true;
    room.isAvailable = true;
    ProtocolRosterEntry member;
    member.id = QStringLiteral("@member:example.org");
    member.isValid = true;
    room.members.append(member);

    const QString activeAccount = QStringLiteral("persistent-account-A");
    const QString roomId = QStringLiteral("!room-A:example.org");
    const QString memberId = QStringLiteral("@member:example.org");
    bool passed = true;
    passed &= expect(MatrixRoomMemberActionPolicy::directConversationAllowed(
        activeAccount, activeAccount, true, roomId, room, memberId,
        QStringLiteral("@self:example.org")), "joined member in the bound account is actionable");
    passed &= expect(!MatrixRoomMemberActionPolicy::directConversationAllowed(
        QStringLiteral("persistent-account-B"), activeAccount, true, roomId, room, memberId,
        QStringLiteral("@self:example.org")), "member actions are account-bound");
    passed &= expect(!MatrixRoomMemberActionPolicy::directConversationAllowed(
        activeAccount, activeAccount, false, roomId, room, memberId,
        QStringLiteral("@self:example.org")), "disconnected sessions expose no action");
    passed &= expect(!MatrixRoomMemberActionPolicy::directConversationAllowed(
        activeAccount, activeAccount, true, QStringLiteral("!other:example.org"), room, memberId,
        QStringLiteral("@self:example.org")), "room identity must match the clicked room");
    passed &= expect(!MatrixRoomMemberActionPolicy::directConversationAllowed(
        activeAccount, activeAccount, true, roomId, room, QStringLiteral("@other:example.org"),
        QStringLiteral("@self:example.org")), "only current room members are actionable");
    passed &= expect(!MatrixRoomMemberActionPolicy::directConversationAllowed(
        activeAccount, activeAccount, true, roomId, room, memberId, memberId),
        "the current user cannot open a direct conversation with themself");

    room.members[0].isValid = false;
    passed &= expect(!MatrixRoomMemberActionPolicy::directConversationAllowed(
        activeAccount, activeAccount, true, roomId, room, memberId,
        QStringLiteral("@self:example.org")), "non-joined members are not actionable");

    QJsonObject powerLevels;
    powerLevels.insert(QStringLiteral("users_default"), 0);
    powerLevels.insert(QStringLiteral("state_default"), 50);
    powerLevels.insert(QStringLiteral("kick"), 50);
    powerLevels.insert(QStringLiteral("ban"), 50);
    powerLevels.insert(QStringLiteral("redact"), 50);
    QJsonObject users;
    users.insert(QStringLiteral("@admin:example.org"), 100);
    powerLevels.insert(QStringLiteral("users"), users);
    const auto adminActions = MatrixRoomMemberActionPolicy::moderationActions(true,
        powerLevels, QStringLiteral("@admin:example.org"), memberId);
    passed &= expect(adminActions.canSetRole && adminActions.canKick && adminActions.canBan &&
        adminActions.canRedact, "sufficient room power grants available moderator actions");
    const QList<ProtocolRoomMemberRole> assignableRoles =
        MatrixRoomMemberActionPolicy::rolesForMember(true, powerLevels,
            QStringLiteral("@admin:example.org"), memberId);
    passed &= expect(assignableRoles.size() == 2 && assignableRoles.at(0).id == QStringLiteral("50") &&
        assignableRoles.at(0).label == QStringLiteral("Moderator") &&
        assignableRoles.at(1).id == QStringLiteral("0") &&
        assignableRoles.at(1).label == QStringLiteral("Member"),
        "role options are generic and limited below the actor's own power level");
    const auto unknownState = MatrixRoomMemberActionPolicy::moderationActions(false,
        powerLevels, QStringLiteral("@admin:example.org"), memberId);
    passed &= expect(!unknownState.canSetRole && !unknownState.canKick && !unknownState.canBan &&
        !unknownState.canRedact, "unknown room permissions fail closed");
    users.insert(memberId, 100);
    powerLevels.insert(QStringLiteral("users"), users);
    const auto equalPower = MatrixRoomMemberActionPolicy::moderationActions(true,
        powerLevels, QStringLiteral("@admin:example.org"), memberId);
    passed &= expect(!equalPower.canSetRole && !equalPower.canKick && !equalPower.canBan &&
        equalPower.canRedact, "equal-power target cannot be demoted, kicked, or banned");
    users.insert(QStringLiteral("@admin:example.org"), 49);
    users.insert(memberId, 0);
    powerLevels.insert(QStringLiteral("users"), users);
    const auto insufficientPower = MatrixRoomMemberActionPolicy::moderationActions(true,
        powerLevels, QStringLiteral("@admin:example.org"), memberId);
    passed &= expect(!insufficientPower.canSetRole && !insufficientPower.canKick &&
        !insufficientPower.canBan && !insufficientPower.canRedact,
        "sender below each default moderation threshold has no action");
    return passed ? 0 : 1;
}
