#include "protocolcontactactionpolicy.h"

#include <iostream>

class TestCapabilities : public IProtocolCapabilities
{
public:
    bool ready = true;

    Capabilities capabilitiesForAccount(const AccountId &accountId,
        const ConversationId &targetId = ConversationId()) const override
    {
        Q_UNUSED(targetId);
        if (ready && accountId == QStringLiteral("active-account"))
            return CapabilityAddContact;
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
    return failures == 0 ? 0 : 1;
}
