#ifndef MATRIXMEMBERDISPLAYNAMECACHE_H
#define MATRIXMEMBERDISPLAYNAMECACHE_H

#include "matrixmemberdisplaynameversion.h"

#include <QHash>
#include <QList>
#include <QString>
#include <algorithm>
#include <functional>
#include <iterator>

class MatrixMemberDisplayNameCache
{
public:
    using HistoryLoader = std::function<QList<MatrixMemberDisplayNameVersion>(
        const QString &, const QString &)>;

    QString displayName(const QString &roomId, const QString &userId, qint64 timestamp,
                        const HistoryLoader &loader)
    {
        if (roomId.isEmpty() || userId.isEmpty())
            return QString();

        Entry &entry = FEntries[roomId][userId];
        if (!entry.loaded) {
            entry.loaded = true;
            if (loader)
                entry.versions = loader(roomId, userId);
            std::stable_sort(entry.versions.begin(), entry.versions.end(),
                [](const MatrixMemberDisplayNameVersion &left,
                   const MatrixMemberDisplayNameVersion &right) {
                    return left.validFrom < right.validFrom;
                });
        }

        const auto afterTimestamp = std::upper_bound(entry.versions.cbegin(),
            entry.versions.cend(), timestamp,
            [](qint64 value, const MatrixMemberDisplayNameVersion &version) {
                return value < version.validFrom;
            });
        if (afterTimestamp == entry.versions.cbegin())
            return QString();
        return std::prev(afterTimestamp)->displayName;
    }

private:
    struct Entry
    {
        bool loaded = false;
        QList<MatrixMemberDisplayNameVersion> versions;
    };

    QHash<QString, QHash<QString, Entry>> FEntries;
};

#endif // MATRIXMEMBERDISPLAYNAMECACHE_H
