#ifndef IPROTOCOLROOMMEMBERACTIONS_H
#define IPROTOCOLROOMMEMBERACTIONS_H

#include <QFlags>
#include <QList>
#include <QObject>
#include <QtPlugin>
#include <functional>
#include <interfaces/identity.h>

struct ProtocolRoomMemberRole
{
    QString id;
    QString label;
};

// Optional room-member actions that a protocol adapter can expose to common UI.
// Capabilities are scoped by persistent account, room, and member identity.
class IProtocolRoomMemberActions
{
public:
    enum Action
    {
        ActionNone = 0x0,
        ActionSetRole = 1 << 0,
        ActionKick = 1 << 1,
        ActionBan = 1 << 2,
        ActionRedactMessages = 1 << 3,
        ActionOpenDirectConversation = 1 << 4,
        ActionJumpToReadReceipt = 1 << 5,
        ActionShareProfile = 1 << 6,
        ActionMentionMember = 1 << 7
    };
    Q_DECLARE_FLAGS(MemberActions, Action)
    using MemberActionsUpdatedCallback = std::function<void(MemberActions)>;

    virtual ~IProtocolRoomMemberActions() {}

    virtual AccountId accountId() const { return AccountId(); }
    virtual QString streamId() const { return QString(); }

    virtual MemberActions actionsForMember(const AccountId &accountId,
        const ConversationId &roomId, const UserId &memberId) const
    {
        Q_UNUSED(accountId);
        Q_UNUSED(roomId);
        Q_UNUSED(memberId);
        return MemberActions();
    }

    // Returns the current snapshot and reports later changes while callbackContext
    // remains alive. Adapters may use this when action availability is asynchronous.
    virtual MemberActions observeActionsForMember(const AccountId &accountId,
        const ConversationId &roomId, const UserId &memberId, QObject *callbackContext,
        MemberActionsUpdatedCallback callback)
    {
        Q_UNUSED(callbackContext);
        const MemberActions actions = actionsForMember(accountId, roomId, memberId);
        if (callback)
            callback(actions);
        return actions;
    }

    virtual QList<ProtocolRoomMemberRole> memberRoles(const AccountId &accountId,
        const ConversationId &roomId, const UserId &memberId) const
    {
        Q_UNUSED(accountId);
        Q_UNUSED(roomId);
        Q_UNUSED(memberId);
        return QList<ProtocolRoomMemberRole>();
    }

    virtual QString memberRole(const AccountId &accountId,
        const ConversationId &roomId, const UserId &memberId) const
    {
        Q_UNUSED(accountId);
        Q_UNUSED(roomId);
        Q_UNUSED(memberId);
        return QString();
    }


    // Actions are revalidated by the provider when invoked; a UI capability
    // snapshot is not an authorization token and may become stale.
    virtual bool setMemberRole(const AccountId &accountId,
        const ConversationId &roomId, const UserId &memberId, const QString &roleId)
    {
        Q_UNUSED(accountId);
        Q_UNUSED(roomId);
        Q_UNUSED(memberId);
        Q_UNUSED(roleId);
        return false;
    }

    virtual bool kickMember(const AccountId &accountId, const ConversationId &roomId,
        const UserId &memberId, const QString &reason = QString())
    {
        Q_UNUSED(accountId);
        Q_UNUSED(roomId);
        Q_UNUSED(memberId);
        Q_UNUSED(reason);
        return false;
    }

    virtual bool banMember(const AccountId &accountId, const ConversationId &roomId,
        const UserId &memberId, const QString &reason = QString())
    {
        Q_UNUSED(accountId);
        Q_UNUSED(roomId);
        Q_UNUSED(memberId);
        Q_UNUSED(reason);
        return false;
    }

    virtual bool redactMemberMessages(const AccountId &accountId,
        const ConversationId &roomId, const UserId &memberId, const QString &reason = QString())
    {
        Q_UNUSED(accountId);
        Q_UNUSED(roomId);
        Q_UNUSED(memberId);
        Q_UNUSED(reason);
        return false;
    }

    virtual bool openDirectConversation(const AccountId &accountId, const UserId &memberId)
    {
        Q_UNUSED(accountId);
        Q_UNUSED(memberId);
        return false;
    }

    virtual bool jumpToMemberReadReceipt(const AccountId &accountId,
        const ConversationId &roomId, const UserId &memberId)
    {
        Q_UNUSED(accountId);
        Q_UNUSED(roomId);
        Q_UNUSED(memberId);
        return false;
    }

    virtual bool shareMemberProfile(const AccountId &accountId, const UserId &memberId)
    {
        Q_UNUSED(accountId);
        Q_UNUSED(memberId);
        return false;
    }

    virtual bool mentionMember(const AccountId &accountId,
        const ConversationId &roomId, const UserId &memberId)
    {
        Q_UNUSED(accountId);
        Q_UNUSED(roomId);
        Q_UNUSED(memberId);
        return false;
    }

};

Q_DECLARE_OPERATORS_FOR_FLAGS(IProtocolRoomMemberActions::MemberActions)
Q_DECLARE_INTERFACE(IProtocolRoomMemberActions,
    "Vacuum.Plugin.IProtocolRoomMemberActions/1.0")

#endif // IPROTOCOLROOMMEMBERACTIONS_H
