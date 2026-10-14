#ifndef MATRIXROOMMEMBERACTIONPOLICY_H
#define MATRIXROOMMEMBERACTIONPOLICY_H

#include <interfaces/iprotocolroster.h>
#include <interfaces/identity.h>
#include <interfaces/iprotocolroommemberactions.h>
#include <QList>
#include <QJsonObject>
#include <QJsonValue>

namespace MatrixRoomMemberActionPolicy
{
struct ModerationActions
{
    bool canSetRole = false;
    bool canKick = false;
    bool canBan = false;
    bool canRedact = false;
};

inline int userPowerLevel(const QJsonObject &powerLevels, const QString &userId)
{
    const QJsonObject users = powerLevels.value(QStringLiteral("users")).toObject();
    const QJsonValue configured = users.value(userId);
    return configured.isDouble()
        ? configured.toInt() : powerLevels.value(QStringLiteral("users_default")).toInt(0);
}

inline ModerationActions moderationActions(bool stateKnown, const QJsonObject &powerLevels,
    const UserId &ownUserId, const UserId &memberId)
{
    ModerationActions actions;
    if (!stateKnown || ownUserId.isEmpty() || memberId.isEmpty())
        return actions;

    const int ownLevel = userPowerLevel(powerLevels, ownUserId);
    const int memberLevel = userPowerLevel(powerLevels, memberId);
    const int stateDefault = powerLevels.value(QStringLiteral("state_default")).toInt(50);
    const int kickLevel = powerLevels.value(QStringLiteral("kick")).toInt(50);
    const int banLevel = powerLevels.value(QStringLiteral("ban")).toInt(50);
    const int redactLevel = powerLevels.value(QStringLiteral("redact")).toInt(50);
    actions.canSetRole = ownLevel >= stateDefault && ownLevel > memberLevel;
    actions.canKick = ownLevel >= kickLevel && ownLevel > memberLevel;
    actions.canBan = ownLevel >= banLevel && ownLevel > memberLevel;
    actions.canRedact = ownLevel >= redactLevel;
    return actions;
}

inline QList<ProtocolRoomMemberRole> rolesForMember(bool stateKnown,
    const QJsonObject &powerLevels, const UserId &ownUserId, const UserId &memberId)
{
    QList<ProtocolRoomMemberRole> roles;
    if (!moderationActions(stateKnown, powerLevels, ownUserId, memberId).canSetRole)
        return roles;

    const int ownLevel = userPowerLevel(powerLevels, ownUserId);
    const int memberLevel = userPowerLevel(powerLevels, memberId);
    const QList<QPair<int, QString>> standardRoles{
        {100, QStringLiteral("Administrator")},
        {50, QStringLiteral("Moderator")},
        {0, QStringLiteral("Member")}
    };
    for (const auto &role : standardRoles) {
        if (role.first < ownLevel || role.first == memberLevel)
            roles.append({QString::number(role.first), role.second});
    }
    return roles;
}

inline bool directConversationAllowed(const AccountId &requestedAccountId,
    const AccountId &boundAccountId, bool sessionReady, const ConversationId &requestedRoomId,
    const ProtocolRoom &room, const UserId &memberId, const UserId &ownUserId)
{
    if (requestedAccountId.isEmpty() || requestedAccountId != boundAccountId || !sessionReady ||
        requestedRoomId.isEmpty() || room.id != requestedRoomId || !room.isJoined ||
        !room.isAvailable || memberId.isEmpty() || memberId == ownUserId)
        return false;

    for (const ProtocolRosterEntry &member : room.members)
        if (member.id == memberId && member.isValid)
            return true;
    return false;
}
}

#endif // MATRIXROOMMEMBERACTIONPOLICY_H
