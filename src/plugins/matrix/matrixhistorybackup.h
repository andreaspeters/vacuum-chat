#ifndef MATRIXHISTORYBACKUP_H
#define MATRIXHISTORYBACKUP_H

#include <QString>
#include <QList>

#include "matrixnetwork.h"

class MatrixHistoryBackup
{
public:
    MatrixHistoryBackup(const QString &profileDirectory,
        const QString &normalizedServerUrl, const QString &userId);

    bool saveMessageHistory(const QString &roomId,
        const QList<MatrixTextEvent> &history) const;
    bool loadMessageHistory(const QString &roomId,
        QList<MatrixTextEvent> &history) const;
    QString backupFilePath(const QString &roomId) const;

private:
    QString hashString(const QString &value) const;

    QString FProfileDirectory;
    QString FNormalizedServerUrl;
    QString FUserId;
};

#endif // MATRIXHISTORYBACKUP_H
