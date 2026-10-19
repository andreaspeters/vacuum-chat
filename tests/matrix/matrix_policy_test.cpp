#include "matrixavatarkeypolicy.h"
#include "matrixmetadatarefreshpolicy.h"
#include "matrixroommemberpolicy.h"
#include "matrixsessionpolicy.h"

#include <interfaces/ipresence.h>

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

MatrixRoomMemberRedactionPolicy::CachedEvent event(const QString &roomId,
    const QString &eventId, const QString &sender, const QString &eventType,
    bool redacted = false)
{
    return {roomId, eventId, sender, eventType, redacted};
}

bool testRoomMemberActionPolicy()
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
    ProtocolRoom profileRoom;
    profileRoom.id = QStringLiteral("!room-A:example.org");
    profileRoom.isJoined = true;
    profileRoom.isAvailable = true;
    passed &= expect(MatrixRoomMemberActionPolicy::canObserveMemberProfile(
        QStringLiteral("account-A"), QStringLiteral("account-A"), true, profileRoom.id,
        profileRoom, memberId, QStringLiteral("@admin:example.org")),
        "member profile observation is allowed before its member snapshot has loaded");
    profileRoom.members.clear();
    passed &= expect(MatrixRoomMemberActionPolicy::canObserveMemberProfile(
        QStringLiteral("account-A"), QStringLiteral("account-A"), true, profileRoom.id,
        profileRoom, memberId, QStringLiteral("@admin:example.org")),
        "missing target member does not prevent observing asynchronous room updates");
    passed &= expect(!MatrixRoomMemberActionPolicy::canObserveMemberProfile(
        QStringLiteral("account-B"), QStringLiteral("account-A"), true, profileRoom.id,
        profileRoom, memberId, QStringLiteral("@admin:example.org")),
        "member profile observation remains scoped to its owning account");
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
    return passed;
}

bool testRoomMemberRedactionPolicy()
{
    const QString room = QStringLiteral("!room:example.org");
    const QString member = QStringLiteral("@member:example.org");
    const QList<MatrixRoomMemberRedactionPolicy::CachedEvent> events{
        event(room, QStringLiteral("$message"), member, QStringLiteral("m.room.message")),
        event(room, QStringLiteral("$encrypted"), member, QStringLiteral("m.room.encrypted")),
        event(room, QStringLiteral("$sticker"), member, QStringLiteral("m.sticker")),
        event(room, QStringLiteral("$reaction"), member, QStringLiteral("m.reaction")),
        event(room, QStringLiteral("$other-sender"), QStringLiteral("@other:example.org"),
            QStringLiteral("m.room.message")),
        event(QStringLiteral("!other:example.org"), QStringLiteral("$other-room"), member,
            QStringLiteral("m.room.message")),
        event(room, QStringLiteral("$state"), member, QStringLiteral("m.room.topic")),
        event(room, QStringLiteral("$already-redacted"), member,
            QStringLiteral("m.room.message"), true),
        event(room, QString(), member, QStringLiteral("m.room.message")),
        event(room, QStringLiteral("$redaction"), member, QStringLiteral("m.room.redaction")),
        event(room, QStringLiteral("$message"), member, QStringLiteral("m.room.message"))
    };
    const QStringList ids = MatrixRoomMemberRedactionPolicy::eventIdsToRedact(events, room, member);
    bool passed = true;
    passed &= expect(ids == QStringList({QStringLiteral("$message"), QStringLiteral("$encrypted"),
        QStringLiteral("$sticker"), QStringLiteral("$reaction")}),
        "select unique, unredacted message-like events by exact room and sender");
    passed &= expect(MatrixRoomMemberRedactionPolicy::eventIdsToRedact(events, QString(), member).isEmpty(),
        "empty room identity selects no events");
    passed &= expect(MatrixRoomMemberRedactionPolicy::eventIdsToRedact(events, room, QString()).isEmpty(),
        "empty member identity selects no events");
    passed &= expect(MatrixRoomMemberRedactionPolicy::redactionPath(room,
        QStringLiteral("$event:example.org"), QStringLiteral("txn-1")) ==
        QStringLiteral("/_matrix/client/v3/rooms/%21room%3Aexample.org/redact/%24event%3Aexample.org/txn-1"),
        "build the Matrix v3 redaction endpoint from encoded path segments");
    return passed;
}

bool testMetadataRefreshPolicy()
{
    bool passed = true;
    MatrixMetadataRefreshPolicy policy;
    const QString roomA = QStringLiteral("account-a\n!room:a");
    const QString roomB = QStringLiteral("account-a\n!room:b");

    passed &= expect(!policy.shouldCheck(roomA, false, 1000),
        "complete room metadata does not trigger or consume the throttle");
    passed &= expect(policy.shouldCheck(roomA, true, 1000),
        "first missing-metadata check is allowed");
    passed &= expect(!policy.shouldCheck(roomA, true, 600999),
        "repeat check at 599999 ms is suppressed");
    passed &= expect(policy.shouldCheck(roomA, true, 601000),
        "repeat check at exactly 600000 ms is allowed");
    passed &= expect(policy.shouldCheck(roomB, true, 601000),
        "throttle is independent for each room");
    policy.clear();
    passed &= expect(policy.shouldCheck(roomA, true, 601000),
        "clearing the policy resets per-session check history");
    return passed;
}

bool testSessionPolicy()
{
    bool passed = true;
    using MatrixSessionPolicy::PresenceAction;

    passed &= expect(MatrixSessionPolicy::actionForPresence(IPresence::Offline, true) ==
        PresenceAction::Disconnect, "offline selection disconnects an active session");
    passed &= expect(MatrixSessionPolicy::actionForPresence(IPresence::Offline, false) ==
        PresenceAction::Disconnect, "offline selection remains disconnected when already offline");
    passed &= expect(MatrixSessionPolicy::actionForPresence(IPresence::Online, false) ==
        PresenceAction::Connect, "online selection initiates login when disconnected");
    passed &= expect(MatrixSessionPolicy::actionForPresence(IPresence::Away, false) ==
        PresenceAction::Connect, "non-offline selection initiates login when disconnected");
    passed &= expect(MatrixSessionPolicy::actionForPresence(IPresence::Online, true) ==
        PresenceAction::UpdateRemotePresence, "online selection updates presence on an active session");
    passed &= expect(!MatrixSessionPolicy::shouldAcceptLoginResponse(false, true),
        "a stale login response is rejected");
    passed &= expect(!MatrixSessionPolicy::shouldAcceptLoginResponse(true, false),
        "a login response is rejected after offline was requested");
    passed &= expect(MatrixSessionPolicy::shouldAcceptLoginResponse(true, true),
        "a current login response is accepted while connection is requested");
    passed &= expect(MatrixSessionPolicy::matchesLoginUserId(
        QStringLiteral("@alice:example.org"), QStringLiteral("@alice:example.org")),
        "a fully qualified configured Matrix user must match exactly");
    passed &= expect(MatrixSessionPolicy::matchesLoginUserId(
        QStringLiteral("alice"), QStringLiteral("@alice:example.org")),
        "a configured localpart matches the server's canonical Matrix user ID");
    passed &= expect(MatrixSessionPolicy::matchesLoginUserId(
        QStringLiteral("@alice"), QStringLiteral("@alice:example.org")),
        "a localpart prefixed with @ matches the server's canonical Matrix user ID");
    passed &= expect(!MatrixSessionPolicy::matchesLoginUserId(
        QStringLiteral("bob"), QStringLiteral("@alice:example.org")),
        "a different localpart is rejected");
    passed &= expect(!MatrixSessionPolicy::matchesLoginUserId(
        QStringLiteral("@alice:other.org"), QStringLiteral("@alice:example.org")),
        "a different fully qualified Matrix user ID is rejected");
    passed &= expect(!MatrixSessionPolicy::matchesLoginUserId(
        QStringLiteral("alice"), QStringLiteral("alice")),
        "a non-canonical server identity is rejected");
    return passed;
}

bool testAvatarKeyPolicy()
{
    const AccountId accountId = QStringLiteral("matrix-account-a");
    const QString userId = QStringLiteral("@alice:matrix.example");
    const QString oldAvatar = QStringLiteral("mxc://media.example.org/old");
    const QString newAvatar = QStringLiteral("mxc://media.example.org/new");
    const QString oldKey = MatrixAvatarKeyPolicy::userAvatarKey(accountId, userId, oldAvatar);

    bool passed = true;
    const QString expectedOldKey = accountId + QStringLiteral("\nuser\n") + userId +
        QLatin1Char('\n') + QStringLiteral("0e0b25bde8a609a781d3632f427f5ef31f6ca6d060dd68bb56c93c4fb5ebbf9e");
    passed &= expect(oldKey == expectedOldKey,
        "avatar cache key includes the account, user, and stable SHA-256 source hash");
    passed &= expect(oldKey != MatrixAvatarKeyPolicy::userAvatarKey(accountId, userId, newAvatar),
        "changed avatar source must not reuse stale cached pixels");
    passed &= expect(oldKey != MatrixAvatarKeyPolicy::userAvatarKey(accountId, userId, QString()),
        "clearing an avatar must not retain the previous cached image key");
    passed &= expect(oldKey != MatrixAvatarKeyPolicy::userAvatarKey(
        QStringLiteral("matrix-account-b"), userId, oldAvatar),
        "avatar cache keys must remain account-scoped");
    return passed;
}
}

int runMatrixPolicyTests()
{
    bool passed = true;
    passed &= testRoomMemberActionPolicy();
    passed &= testRoomMemberRedactionPolicy();
    passed &= testMetadataRefreshPolicy();
    passed &= testSessionPolicy();
    passed &= testAvatarKeyPolicy();
    return passed ? 0 : 1;
}
