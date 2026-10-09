#include "matrixmetadatarefreshpolicy.h"

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
    bool passed = true;
    MatrixMetadataRefreshPolicy policy;
    const QString roomA = QStringLiteral("account-a\n!room:a");
    const QString roomB = QStringLiteral("account-a\n!room:b");

    passed &= check(!policy.shouldCheck(roomA, false, 1000),
        "complete room metadata does not trigger or consume the throttle");
    passed &= check(policy.shouldCheck(roomA, true, 1000),
        "first missing-metadata check is allowed");
    passed &= check(!policy.shouldCheck(roomA, true, 600999),
        "repeat check at 599999 ms is suppressed");
    passed &= check(policy.shouldCheck(roomA, true, 601000),
        "repeat check at exactly 600000 ms is allowed");
    passed &= check(policy.shouldCheck(roomB, true, 601000),
        "throttle is independent for each room");
    policy.clear();
    passed &= check(policy.shouldCheck(roomA, true, 601000),
        "clearing the policy resets per-session check history");
    return passed ? 0 : 1;
}
