#include "xmpphistorycapabilitypolicy.h"

#include <iostream>

namespace
{
bool check(bool condition, const char *message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}
}

int main()
{
    bool passed = true;
    const IProtocolCapabilities::Capabilities localHistory =
        XmppHistoryCapabilityPolicy::capabilities(true, true, true);
    passed &= check(localHistory.testFlag(IProtocolCapabilities::CapabilityViewHistory) &&
        !localHistory.testFlag(IProtocolCapabilities::CapabilityManageRemoteArchive),
        "configured XMPP account with local archive support exposes local, not remote, history");
    passed &= check(!XmppHistoryCapabilityPolicy::capabilities(false, true, true).testFlag(
        IProtocolCapabilities::CapabilityViewHistory),
        "local history is unavailable without a configured XMPP account");
    passed &= check(!XmppHistoryCapabilityPolicy::capabilities(true, false, true).testFlag(
        IProtocolCapabilities::CapabilityViewHistory),
        "local history is unavailable when no local provider exists");
    passed &= check(!XmppHistoryCapabilityPolicy::capabilities(true, true, false).testFlag(
        IProtocolCapabilities::CapabilityViewHistory),
        "local history is unavailable without a valid conversation ID");
    return passed ? 0 : 1;
}
