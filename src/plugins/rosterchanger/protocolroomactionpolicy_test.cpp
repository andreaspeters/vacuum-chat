#include "protocolroomactionpolicy.h"

#include <iostream>

class TestCapabilities final : public IProtocolCapabilities
{
public:
    bool ready = true;
    bool canJoin = false;
    bool canLeaveSota = false;

    Capabilities capabilitiesForAccount(const AccountId &accountId,
        const ConversationId &targetId = ConversationId()) const override
    {
        if (!ready || accountId != QStringLiteral("active-account"))
            return Capabilities();
        if (targetId.isEmpty() && canJoin)
            return CapabilityJoinRoom;
        if (targetId == QStringLiteral("aprs-group:SOTA") && canLeaveSota)
            return CapabilityLeaveRoom;
        return Capabilities();
    }
};

class TestRoomActions final : public IProtocolRoomActions
{
public:
    int joinCalls = 0;
    int leaveCalls = 0;
    AccountId lastAccountId;
    ConversationId lastRoomId;

    bool showJoinRoomDialog(const AccountId &accountId) override
    {
        ++joinCalls;
        lastAccountId = accountId;
        return true;
    }

    bool leaveRoom(const AccountId &accountId, const ConversationId &roomId) override
    {
        ++leaveCalls;
        lastAccountId = accountId;
        lastRoomId = roomId;
        return true;
    }
};

int main()
{
    int failures = 0;
    const auto check = [&failures](bool condition, const char *message) {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    };

    TestCapabilities capabilities;
    TestRoomActions actions;
    IProtocolCapabilities unsupported;
    capabilities.canJoin = true;
    capabilities.canLeaveSota = true;
    const AccountId accountId = QStringLiteral("active-account");
    const ConversationId roomId = QStringLiteral("aprs-group:SOTA");

    check(!canOfferJoinRoomAction(nullptr, &actions, accountId),
          "missing capability provider hides join action");
    check(!canOfferJoinRoomAction(&capabilities, nullptr, accountId),
          "missing room-action adapter hides join action");
    check(!canOfferJoinRoomAction(&unsupported, &actions, accountId),
          "safe capability defaults hide unsupported join action");
    check(!canOfferJoinRoomAction(&capabilities, &actions, QString()),
          "empty account identity hides join action");
    check(canOfferJoinRoomAction(&capabilities, &actions, accountId),
          "join capability exposes the account-root action");
    check(dispatchJoinRoomAction(&capabilities, &actions, accountId) &&
              actions.joinCalls == 1 && actions.lastAccountId == accountId,
          "join dispatch preserves the exact account identity");

    check(!canOfferLeaveRoomAction(&capabilities, &actions, accountId,
                                   QStringLiteral("DL1AAA")),
          "leave capability is target-specific and does not expose direct chats");
    check(canOfferLeaveRoomAction(&capabilities, &actions, accountId, roomId),
          "leave capability exposes the exact supported room");
    capabilities.ready = false;
    check(!dispatchLeaveRoomAction(&capabilities, &actions, accountId, roomId) &&
              actions.leaveCalls == 0,
          "leave dispatch rechecks readiness after menu creation");
    capabilities.ready = true;
    check(dispatchLeaveRoomAction(&capabilities, &actions, accountId, roomId) &&
              actions.leaveCalls == 1 && actions.lastAccountId == accountId &&
              actions.lastRoomId == roomId,
          "leave dispatch preserves both account and room identity");

    return failures == 0 ? 0 : 1;
}
