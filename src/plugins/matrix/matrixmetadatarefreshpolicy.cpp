#include "matrixmetadatarefreshpolicy.h"

bool MatrixMetadataRefreshPolicy::shouldCheck(const QString &accountRoomKey,
    bool metadataMissing, qint64 monotonicMilliseconds)
{
    if (accountRoomKey.isEmpty() || !metadataMissing)
        return false;

    const auto previous = FLastChecks.constFind(accountRoomKey);
    if (previous != FLastChecks.cend()) {
        if (monotonicMilliseconds < previous.value() ||
            monotonicMilliseconds - previous.value() < IntervalMilliseconds)
            return false;
    }

    FLastChecks.insert(accountRoomKey, monotonicMilliseconds);
    return true;
}

void MatrixMetadataRefreshPolicy::clear()
{
    FLastChecks.clear();
}
