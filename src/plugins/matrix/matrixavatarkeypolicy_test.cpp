#include "matrixavatarkeypolicy.h"

#include <iostream>

static bool check(bool condition, const char *message)
{
    if (!condition)
        std::cerr << message << '\n';
    return condition;
}

int main()
{
    const AccountId accountId = QStringLiteral("matrix-account-a");
    const QString userId = QStringLiteral("@alice:matrix.example");
    const QString oldAvatar = QStringLiteral("mxc://media.example.org/old");
    const QString newAvatar = QStringLiteral("mxc://media.example.org/new");
    const QString oldKey = MatrixAvatarKeyPolicy::userAvatarKey(accountId, userId, oldAvatar);

    bool success = true;
    success &= check(oldKey == MatrixAvatarKeyPolicy::userAvatarKey(accountId, userId, oldAvatar),
        "same avatar source should produce a stable cache key");
    success &= check(oldKey != MatrixAvatarKeyPolicy::userAvatarKey(accountId, userId, newAvatar),
        "changed avatar source must not reuse stale cached pixels");
    success &= check(oldKey != MatrixAvatarKeyPolicy::userAvatarKey(accountId, userId, QString()),
        "clearing an avatar must not retain the previous cached image key");
    success &= check(oldKey != MatrixAvatarKeyPolicy::userAvatarKey(
        QStringLiteral("matrix-account-b"), userId, oldAvatar),
        "avatar cache keys must remain account-scoped");
    return success ? 0 : 1;
}
