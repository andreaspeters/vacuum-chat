#include "protocolrosterlifecycle.h"

#include <iostream>

int main()
{
    const auto logout = ProtocolRosterLifecycle::transition(QStringLiteral("old"), QString());
    if (logout.streamIdToClose != QStringLiteral("old") || logout.shouldBuildStream) {
        std::cerr << "logout must close the previous stream without building an empty stream\n";
        return 1;
    }

    const auto accountSwitch = ProtocolRosterLifecycle::transition(
        QStringLiteral("old"), QStringLiteral("new"));
    if (accountSwitch.streamIdToClose != QStringLiteral("old") || !accountSwitch.shouldBuildStream) {
        std::cerr << "account switch must close the previous stream and build the new one\n";
        return 1;
    }

    const auto refresh = ProtocolRosterLifecycle::transition(
        QStringLiteral("same"), QStringLiteral("same"));
    if (!refresh.streamIdToClose.isEmpty() || !refresh.shouldBuildStream) {
        std::cerr << "refreshing the same stream must not close it\n";
        return 1;
    }

    return 0;
}
