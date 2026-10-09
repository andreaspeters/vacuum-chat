#ifndef MATRIXMETADATAREFRESHPOLICY_H
#define MATRIXMETADATAREFRESHPOLICY_H

#include <QHash>
#include <QString>

class MatrixMetadataRefreshPolicy
{
public:
    static constexpr qint64 IntervalMilliseconds = 10 * 60 * 1000;

    bool shouldCheck(const QString &accountRoomKey, bool metadataMissing, qint64 monotonicMilliseconds);
    void clear();

private:
    QHash<QString, qint64> FLastChecks;
};

#endif
