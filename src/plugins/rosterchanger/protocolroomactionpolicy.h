#ifndef PROTOCOLROOMACTIONPOLICY_H
#define PROTOCOLROOMACTIONPOLICY_H

#include <interfaces/iprotocolcapabilities.h>
#include <interfaces/iprotocolroomactions.h>

inline bool canOfferJoinRoomAction(const IProtocolCapabilities *capabilities,
    const IProtocolRoomActions *actions, const AccountId &accountId)
{
    return capabilities && actions && !accountId.isEmpty() &&
        capabilities->hasCapabilities(accountId, IProtocolCapabilities::CapabilityJoinRoom);
}

inline bool dispatchJoinRoomAction(const IProtocolCapabilities *capabilities,
    IProtocolRoomActions *actions, const AccountId &accountId)
{
    return canOfferJoinRoomAction(capabilities, actions, accountId) &&
        actions->showJoinRoomDialog(accountId);
}

inline bool canOfferLeaveRoomAction(const IProtocolCapabilities *capabilities,
    const IProtocolRoomActions *actions, const AccountId &accountId,
    const ConversationId &roomId)
{
    return capabilities && actions && !accountId.isEmpty() && !roomId.isEmpty() &&
        capabilities->hasCapabilities(accountId, IProtocolCapabilities::CapabilityLeaveRoom,
                                      roomId);
}

inline bool dispatchLeaveRoomAction(const IProtocolCapabilities *capabilities,
    IProtocolRoomActions *actions, const AccountId &accountId,
    const ConversationId &roomId)
{
    return canOfferLeaveRoomAction(capabilities, actions, accountId, roomId) &&
        actions->leaveRoom(accountId, roomId);
}

#endif // PROTOCOLROOMACTIONPOLICY_H
