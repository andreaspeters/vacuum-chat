#include <interfaces/iprotocolcapabilities.h>
#include "protocolsoftwareversionpolicy.h"

#include <iostream>
#include <utility>

namespace
{
class TargetCapabilities final : public IProtocolCapabilities
{
public:
    TargetCapabilities(AccountId account, ConversationId target, bool enabled = true)
        : supportedAccount(std::move(account)), supportedTarget(std::move(target)), enabled(enabled) {}

    Capabilities capabilitiesForAccount(const AccountId &accountId,
        const ConversationId &targetId = ConversationId()) const override
    {
        return enabled && accountId == supportedAccount && targetId == supportedTarget
            ? Capabilities(CapabilityQuerySoftwareVersion) : Capabilities();
    }

    bool enabled;

private:
    AccountId supportedAccount;
    ConversationId supportedTarget;
};

bool check(bool condition, const char *message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}
}

int main()
{
    IProtocolCapabilities unsupported;
    TargetCapabilities supported(QStringLiteral("account-A"), QStringLiteral("contact-B"));
    TargetCapabilities disabled(QStringLiteral("account-A"), QStringLiteral("contact-B"), false);
    bool passed = true;

    passed &= check(!ClientInfoPolicy::canQuerySoftwareVersion(
        nullptr, QStringLiteral("account-A"), QStringLiteral("contact-B")),
        "missing capability provider is unsupported");
    passed &= check(!ClientInfoPolicy::canQuerySoftwareVersion(
        &unsupported, QStringLiteral("account-A"), QStringLiteral("contact-B")),
        "safe default does not offer software version");
    passed &= check(!ClientInfoPolicy::canQuerySoftwareVersion(
        &supported, QString(), QStringLiteral("contact-B")),
        "empty account ID is rejected");
    passed &= check(!ClientInfoPolicy::canQuerySoftwareVersion(
        &supported, QStringLiteral("account-A"), QString()),
        "empty target ID is rejected");
    passed &= check(!ClientInfoPolicy::canQuerySoftwareVersion(
        &supported, QStringLiteral("account-other"), QStringLiteral("contact-B")),
        "capability is scoped to the matching account");
    passed &= check(!ClientInfoPolicy::canQuerySoftwareVersion(
        &supported, QStringLiteral("account-A"), QStringLiteral("contact-other")),
        "capability is scoped to the matching target");
    passed &= check(ClientInfoPolicy::canQuerySoftwareVersion(
        &supported, QStringLiteral("account-A"), QStringLiteral("contact-B")),
        "supported target offers software version");
    passed &= check(!ClientInfoPolicy::canQuerySoftwareVersion(
        &disabled, QStringLiteral("account-A"), QStringLiteral("contact-B")),
        "disabled capability is not offered");

    return passed ? 0 : 1;
}
