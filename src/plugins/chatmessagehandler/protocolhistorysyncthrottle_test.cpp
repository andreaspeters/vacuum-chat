#include "protocolhistorysyncthrottle.h"

#include <iostream>

namespace {
bool check(bool condition, const char *description)
{
    if (!condition)
        std::cerr << "FAIL: " << description << '\n';
    return condition;
}
}

int main()
{
    ProtocolHistorySyncThrottle throttle;
    bool passed = true;
    const QString stream = QStringLiteral("account-A");
    const QString room = QStringLiteral("room-1");

    passed &= check(throttle.shouldRequest(stream, room, 1000),
        "the first room entry is eligible");
    throttle.recordAccepted(stream, room, 1000);
    passed &= check(!throttle.shouldRequest(stream, room, 1000 + 599999),
        "a repeat entry before ten minutes is suppressed");
    passed &= check(throttle.shouldRequest(stream, room, 1000 + 600000),
        "a repeat entry exactly at ten minutes is eligible");
    passed &= check(throttle.shouldRequest(stream, QStringLiteral("room-2"), 1001),
        "another room is throttled independently");
    passed &= check(throttle.shouldRequest(QStringLiteral("account-B"), room, 1001),
        "another provider stream is throttled independently");
    return passed ? 0 : 1;
}
