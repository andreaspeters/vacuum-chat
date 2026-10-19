#ifndef MATRIXROOMMEMBERPOLICY_H
#define MATRIXROOMMEMBERPOLICY_H

#include <interfaces/iprotocolroster.h>
#include <interfaces/identity.h>
#include <interfaces/iprotocolroommemberactions.h>

#include <algorithm>

#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>

namespace MatrixRoomMemberActionPolicy
{
struct ModerationActions
{
    bool canSetRole = false;
    bool canKick = false;
    bool canBan = false;
    bool canRedact = false;
};

namespace Detail
{
inline bool memberContextValid(const AccountId &requestedAccountId,
    const AccountId &boundAccountId, bool sessionReady, const ConversationId &requestedRoomId,
    const ProtocolRoom &room, const UserId &memberId, const UserId &ownUserId)
{
    return !requestedAccountId.isEmpty() && requestedAccountId == boundAccountId && sessionReady &&
        !requestedRoomId.isEmpty() && room.id == requestedRoomId && room.isJoined &&
        room.isAvailable && !memberId.isEmpty() && memberId != ownUserId;
}

inline bool powerLevelContextValid(bool stateKnown, const UserId &ownUserId,
    const UserId &memberId)
{
    return stateKnown && !ownUserId.isEmpty() && !memberId.isEmpty();
}

inline bool canModerateMember(int ownLevel, int memberLevel, int stateDefault)
{
    return ownLevel >= stateDefault && ownLevel > memberLevel;
}
}

inline bool canObserveMemberProfile(const AccountId &requestedAccountId,
    const AccountId &boundAccountId, bool sessionReady, const ConversationId &requestedRoomId,
    const ProtocolRoom &room, const UserId &memberId, const UserId &ownUserId)
{
    return Detail::memberContextValid(requestedAccountId, boundAccountId, sessionReady,
        requestedRoomId, room, memberId, ownUserId);
}

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
    if (!Detail::powerLevelContextValid(stateKnown, ownUserId, memberId))
        return actions;

    const int ownLevel = userPowerLevel(powerLevels, ownUserId);
    const int memberLevel = userPowerLevel(powerLevels, memberId);
    const int stateDefault = powerLevels.value(QStringLiteral("state_default")).toInt(50);
    const int kickLevel = powerLevels.value(QStringLiteral("kick")).toInt(50);
    const int banLevel = powerLevels.value(QStringLiteral("ban")).toInt(50);
    const int redactLevel = powerLevels.value(QStringLiteral("redact")).toInt(50);
    actions.canSetRole = Detail::canModerateMember(ownLevel, memberLevel, stateDefault);
    actions.canKick = Detail::canModerateMember(ownLevel, memberLevel, kickLevel);
    actions.canBan = Detail::canModerateMember(ownLevel, memberLevel, banLevel);
    actions.canRedact = ownLevel >= redactLevel;
    return actions;
}

inline QList<ProtocolRoomMemberRole> rolesForMember(bool stateKnown,
    const QJsonObject &powerLevels, const UserId &ownUserId, const UserId &memberId)
{
    QList<ProtocolRoomMemberRole> roles;
    if (!Detail::powerLevelContextValid(stateKnown, ownUserId, memberId))
        return roles;

    const int ownLevel = userPowerLevel(powerLevels, ownUserId);
    const int memberLevel = userPowerLevel(powerLevels, memberId);
    const int stateDefault = powerLevels.value(QStringLiteral("state_default")).toInt(50);
    if (!Detail::canModerateMember(ownLevel, memberLevel, stateDefault))
        return roles;
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
    if (!Detail::memberContextValid(requestedAccountId, boundAccountId, sessionReady,
            requestedRoomId, room, memberId, ownUserId))
        return false;

    return std::any_of(room.members.cbegin(), room.members.cend(), [&memberId](
        const ProtocolRosterEntry &member) {
        return member.id == memberId && member.isValid;
    });
}
}

namespace MatrixRoomMemberRedactionPolicy
{
struct CachedEvent
{
    QString roomId;
    QString eventId;
    QString sender;
    QString eventType;
    bool redacted = false;
};

inline QStringList eventIdsToRedact(const QList<CachedEvent> &events,
    const QString &roomId, const QString &memberId)
{
    if (roomId.isEmpty() || memberId.isEmpty())
        return {};

    QStringList eventIds;
    QSet<QString> seen;
    for (const CachedEvent &event : events) {
        if (event.roomId != roomId || event.sender != memberId || event.eventId.isEmpty() ||
            event.redacted || seen.contains(event.eventId))
            continue;

        const bool messageLike = event.eventType == QStringLiteral("m.room.message") ||
            event.eventType == QStringLiteral("m.room.encrypted") ||
            event.eventType == QStringLiteral("m.sticker") ||
            event.eventType == QStringLiteral("m.reaction");
        if (!messageLike)
            continue;

        seen.insert(event.eventId);
        eventIds.append(event.eventId);
    }
    return eventIds;
}

inline QString redactionPath(const QString &roomId, const QString &eventId,
    const QString &transactionId)
{
    if (roomId.isEmpty() || eventId.isEmpty() || transactionId.isEmpty())
        return QString();
    return QStringLiteral("/_matrix/client/v3/rooms/%1/redact/%2/%3")
        .arg(QString::fromLatin1(QUrl::toPercentEncoding(roomId)),
            QString::fromLatin1(QUrl::toPercentEncoding(eventId)),
            QString::fromLatin1(QUrl::toPercentEncoding(transactionId)));
}
}

#endif // MATRIXROOMMEMBERPOLICY_H