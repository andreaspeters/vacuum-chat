#ifndef IPROTOCOLROOMACTIONS_H
#define IPROTOCOLROOMACTIONS_H

#include <QtPlugin>
#include <interfaces/identity.h>

class IProtocolRoomActions
{
public:
    virtual ~IProtocolRoomActions() {}

    // Show the adapter's room/group join dialog for this persistent account.
    virtual bool showJoinRoomDialog(const AccountId &accountId) = 0;

    // Leave a room identified by the adapter's stable conversation ID.
    virtual bool leaveRoom(const AccountId &accountId, const ConversationId &roomId) = 0;
};

Q_DECLARE_INTERFACE(IProtocolRoomActions, "Vacuum.Plugin.IProtocolRoomActions/1.0")

#endif // IPROTOCOLROOMACTIONS_H
