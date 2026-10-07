#ifndef MATRIXAVATARKEYPOLICY_H
#define MATRIXAVATARKEYPOLICY_H

#include <QCryptographicHash>
#include <QString>
#include <interfaces/identity.h>

namespace MatrixAvatarKeyPolicy
{
inline QString userAvatarKey(const AccountId &accountId, const QString &userId,
    const QString &avatarUrl)
{
    const QString avatarSourceHash = QString::fromLatin1(
        QCryptographicHash::hash(avatarUrl.toUtf8(), QCryptographicHash::Sha256).toHex());
    return accountId + QStringLiteral("\nuser\n") + userId + QLatin1Char('\n') + avatarSourceHash;
}
}

#endif // MATRIXAVATARKEYPOLICY_H
