#include "protocolcontactactionpolicy.h"

#include <iostream>
#include <type_traits>
#include <interfaces/iprotocoladvertactions.h>

template<typename...>
struct MakeVoid { typedef void type; };

template<typename T, typename = void>
struct HasAdvertCapabilities : std::false_type {};

template<typename T>
struct HasAdvertCapabilities<T, typename MakeVoid<
    decltype(T::CapabilitySendZeroHopAdvert),
    decltype(T::CapabilitySendFloodAdvert)>::type> : std::true_type {};

static_assert(HasAdvertCapabilities<IProtocolCapabilities>::value,
    "MeshCore self-advert capabilities must be available to the shared menu");

class TestCapabilities : public IProtocolCapabilities
{
public:
    bool ready = true;
    Capabilities advertCapabilities;

    Capabilities capabilitiesForAccount(const AccountId &accountId,
        const ConversationId &targetId = ConversationId()) const override
    {
        Q_UNUSED(targetId);
        if (ready && accountId == QStringLiteral("active-account"))
            return CapabilityAddContact | advertCapabilities;
        return Capabilities();
    }
};

class TestContactActions : public IProtocolContactActions
{
public:
    int calls = 0;
    AccountId lastAccountId;

    bool showAddContactDialog(const AccountId &accountId) override
    {
        ++calls;
        lastAccountId = accountId;
        return true;
    }
};

class TestAdvertActions : public IProtocolAdvertActions
{
public:
    int calls = 0;
    AccountId lastAccountId;
    AdvertType lastType = AdvertType::ZeroHop;

    bool sendSelfAdvert(const AccountId &accountId, AdvertType type) override
    {
        ++calls;
        lastAccountId = accountId;
        lastType = type;
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
    TestContactActions actions;
    IProtocolCapabilities unsupported;

    check(!canOfferAddContactAction(nullptr, &actions, QStringLiteral("active-account")),
        "missing capability provider hides the action");
    check(!canOfferAddContactAction(&capabilities, nullptr, QStringLiteral("active-account")),
        "missing adapter operation hides the action");
    check(!canOfferAddContactAction(&unsupported, &actions, QStringLiteral("active-account")),
        "safe default hides an unsupported operation");
    check(!canOfferAddContactAction(&capabilities, &actions, QStringLiteral("other-account")),
        "capability is scoped to the matching account");
    check(canOfferAddContactAction(&capabilities, &actions, QStringLiteral("active-account")),
        "ready account with a matching operation exposes the action");

    capabilities.ready = false;
    check(!dispatchAddContactAction(&capabilities, &actions, QStringLiteral("active-account")),
        "dispatch rechecks readiness after the menu was created");
    check(actions.calls == 0, "stale capability does not open a dialog");

    capabilities.ready = true;
    check(dispatchAddContactAction(&capabilities, &actions, QStringLiteral("active-account")),
        "current capability dispatches to the adapter");
    check(actions.calls == 1 && actions.lastAccountId == QStringLiteral("active-account"),
        "dispatch forwards the exact account identity");
    TestAdvertActions advertActions;
    const AccountId activeAccountId = QStringLiteral("active-account");
    capabilities.advertCapabilities = IProtocolCapabilities::CapabilitySendZeroHopAdvert;
    check(canOfferSelfAdvertAction(&capabilities, &advertActions, activeAccountId,
              IProtocolAdvertActions::AdvertType::ZeroHop),
        "zero-hop advert is exposed when its specific capability is ready");
    check(!canOfferSelfAdvertAction(&capabilities, &advertActions, activeAccountId,
              IProtocolAdvertActions::AdvertType::Flood),
        "zero-hop capability does not expose a flood advert");
    check(!canOfferSelfAdvertAction(&capabilities, &advertActions, AccountId(),
              IProtocolAdvertActions::AdvertType::ZeroHop),
        "empty account identity hides the advert action");
    check(dispatchSelfAdvertAction(&capabilities, &advertActions, activeAccountId,
              IProtocolAdvertActions::AdvertType::ZeroHop),
        "zero-hop advert dispatches through the protocol adapter");
    check(advertActions.calls == 1 && advertActions.lastAccountId == activeAccountId &&
              advertActions.lastType == IProtocolAdvertActions::AdvertType::ZeroHop,
        "dispatch preserves the account identity and requested advert type");

    capabilities.advertCapabilities = IProtocolCapabilities::CapabilitySendFloodAdvert;
    capabilities.ready = false;
    check(!dispatchSelfAdvertAction(&capabilities, &advertActions, activeAccountId,
              IProtocolAdvertActions::AdvertType::Flood),
        "dispatch rechecks session readiness after menu creation");
    check(advertActions.calls == 1, "stale menu action does not invoke the adapter");
    capabilities.ready = true;
    check(dispatchSelfAdvertAction(&capabilities, &advertActions, activeAccountId,
              IProtocolAdvertActions::AdvertType::Flood),
        "flood advert is independently capability-gated and dispatched");
    check(advertActions.calls == 2 && advertActions.lastType ==
              IProtocolAdvertActions::AdvertType::Flood,
        "flood dispatch reaches the adapter with the flood type");

    IProtocolAdvertActions unsupportedAdvertActions;
    check(!dispatchSelfAdvertAction(&capabilities, &unsupportedAdvertActions,
              activeAccountId, IProtocolAdvertActions::AdvertType::Flood),
        "safe default advert operation refuses an unsupported dispatch");
    return failures == 0 ? 0 : 1;
}
