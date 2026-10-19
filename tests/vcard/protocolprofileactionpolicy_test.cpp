#include <QCoreApplication>

#include <interfaces/identity.h>
#include <interfaces/iprotocolcapabilities.h>
#include <interfaces/iprotocolprofileactions.h>

#include <interfaces/protocolprofileactionpolicy.h>

#include <iostream>

namespace
{
const AccountId TestAccountId = QStringLiteral("persistent-account-id");
const UserId TestUserId = QStringLiteral("@member:example.test");

bool check(bool condition, const char *message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << std::endl;
    return condition;
}

class TestCapabilities : public IProtocolCapabilities
{
public:
    bool ready = true;
    bool showSupported = false;
    bool editSupported = false;

    Capabilities capabilitiesForAccount(const AccountId &accountId,
        const ConversationId &targetId = ConversationId()) const override
    {
        Capabilities capabilities;
        if (!ready || accountId != TestAccountId)
            return capabilities;
        if (showSupported && targetId == TestUserId)
            capabilities |= CapabilityShowProfile;
        if (editSupported && targetId.isEmpty())
            capabilities |= CapabilityEditProfile;
        return capabilities;
    }
};

class TestProfileActions : public IProtocolProfileActions
{
public:
    int showCalls = 0;
    int editCalls = 0;
    AccountId shownAccountId;
    UserId shownUserId;
    AccountId editedAccountId;

    bool showProfile(const AccountId &accountId, const UserId &userId) override
    {
        ++showCalls;
        shownAccountId = accountId;
        shownUserId = userId;
        return true;
    }

    bool editProfile(const AccountId &accountId) override
    {
        ++editCalls;
        editedAccountId = accountId;
        return true;
    }
};

bool testUnsupportedProviderIsNotOfferedOrDispatched()
{
    IProtocolCapabilities unsupported;
    TestProfileActions actions;
    bool passed = true;
    passed &= check(!ProtocolProfileActionPolicy::canShowProfile(
        &unsupported, &actions, TestAccountId, TestUserId),
        "a provider with default capabilities does not offer Show Profile");
    passed &= check(!ProtocolProfileActionPolicy::canEditProfile(
        &unsupported, &actions, TestAccountId),
        "a provider with default capabilities does not offer Edit Profile");
    passed &= check(!ProtocolProfileActionPolicy::dispatchShowProfile(
        &unsupported, &actions, TestAccountId, TestUserId),
        "an unsupported Show Profile operation is rejected at dispatch");
    passed &= check(!ProtocolProfileActionPolicy::dispatchEditProfile(
        &unsupported, &actions, TestAccountId),
        "an unsupported Edit Profile operation is rejected at dispatch");
    passed &= check(actions.showCalls == 0 && actions.editCalls == 0,
        "unsupported operations never reach the action provider");
    passed &= check(!ProtocolProfileActionPolicy::canEditProfile(
        &unsupported, nullptr, TestAccountId),
        "a missing action interface does not offer Edit Profile");
    return passed;
}

bool testShowProfileIsScopedAndRechecked()
{
    TestCapabilities capabilities;
    TestProfileActions actions;
    capabilities.showSupported = true;
    bool passed = true;
    passed &= check(ProtocolProfileActionPolicy::canShowProfile(
        &capabilities, &actions, TestAccountId, TestUserId),
        "Show Profile is offered for its supported account and user");
    passed &= check(!ProtocolProfileActionPolicy::canShowProfile(
        &capabilities, &actions, QStringLiteral("different-account"), TestUserId),
        "Show Profile is not offered for a different persistent account");
    passed &= check(!ProtocolProfileActionPolicy::canShowProfile(
        &capabilities, &actions, TestAccountId, QStringLiteral("@other:example.test")),
        "Show Profile is not offered for a different user");
    passed &= check(!ProtocolProfileActionPolicy::canShowProfile(
        &capabilities, &actions, TestAccountId, UserId()),
        "Show Profile requires a target user");
    passed &= check(!ProtocolProfileActionPolicy::canShowProfile(
        &capabilities, &actions, AccountId(), TestUserId),
        "Show Profile requires a persistent account ID");
    passed &= check(ProtocolProfileActionPolicy::dispatchShowProfile(
        &capabilities, &actions, TestAccountId, TestUserId),
        "a ready Show Profile operation dispatches");
    passed &= check(actions.showCalls == 1 && actions.shownAccountId == TestAccountId &&
        actions.shownUserId == TestUserId,
        "Show Profile receives the exact persistent account and target user IDs");
    capabilities.ready = false;
    passed &= check(!ProtocolProfileActionPolicy::dispatchShowProfile(
        &capabilities, &actions, TestAccountId, TestUserId) && actions.showCalls == 1,
        "Show Profile rechecks live readiness when invoked");
    return passed;
}

bool testEditProfileIsScopedAndRechecked()
{
    TestCapabilities capabilities;
    TestProfileActions actions;
    capabilities.editSupported = true;
    bool passed = true;
    passed &= check(ProtocolProfileActionPolicy::canEditProfile(
        &capabilities, &actions, TestAccountId),
        "Edit Profile is offered for its supported account");
    passed &= check(!ProtocolProfileActionPolicy::canEditProfile(
        &capabilities, &actions, QStringLiteral("different-account")),
        "Edit Profile is not offered for a different persistent account");
    passed &= check(!ProtocolProfileActionPolicy::canEditProfile(
        &capabilities, &actions, AccountId()),
        "Edit Profile requires a persistent account ID");
    passed &= check(ProtocolProfileActionPolicy::dispatchEditProfile(
        &capabilities, &actions, TestAccountId),
        "a ready Edit Profile operation dispatches");
    passed &= check(actions.editCalls == 1 && actions.editedAccountId == TestAccountId,
        "Edit Profile receives the exact persistent account ID");
    capabilities.ready = false;
    passed &= check(!ProtocolProfileActionPolicy::dispatchEditProfile(
        &capabilities, &actions, TestAccountId) && actions.editCalls == 1,
        "Edit Profile rechecks live readiness when invoked");
    return passed;
}

bool testProviderBindingUsesPersistentIdentity()
{
    const AccountId accountId = QStringLiteral("persistent-account-42");
    const QString providerStreamId = QStringLiteral("provider-stream-9");
    bool passed = true;
    passed &= check(ProtocolProfileActionPolicy::matchesProviderBinding(
        false, AccountId(), QString(), accountId, QString()),
        "a non-roster provider can be considered using the persistent account ID alone");
    passed &= check(ProtocolProfileActionPolicy::matchesProviderBinding(
        true, accountId, providerStreamId, accountId, providerStreamId),
        "a roster provider matches only its exact account and stream IDs");
    passed &= check(!ProtocolProfileActionPolicy::matchesProviderBinding(
        true, accountId, QStringLiteral("other-stream"), accountId, providerStreamId),
        "a roster provider with a different stream ID is rejected");
    passed &= check(!ProtocolProfileActionPolicy::matchesProviderBinding(
        true, QStringLiteral("other-account"), providerStreamId, accountId, providerStreamId),
        "a roster provider with a different account ID is rejected");
    passed &= check(!ProtocolProfileActionPolicy::matchesProviderBinding(
        true, accountId, providerStreamId, accountId, QString()),
        "a roster provider cannot bind without the requested stream ID");
    passed &= check(!ProtocolProfileActionPolicy::matchesProviderBinding(
        false, AccountId(), QString(), AccountId(), QString()),
        "an empty persistent account ID is always rejected");
    return passed;
}
}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    bool passed = true;
    passed &= testUnsupportedProviderIsNotOfferedOrDispatched();
    passed &= testShowProfileIsScopedAndRechecked();
    passed &= testEditProfileIsScopedAndRechecked();
    passed &= testProviderBindingUsesPersistentIdentity();
    return passed ? 0 : 1;
}
