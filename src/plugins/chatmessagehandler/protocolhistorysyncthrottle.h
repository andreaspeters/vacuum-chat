#ifndef PROTOCOLHISTORYSYNCTHROTTLE_H
#define PROTOCOLHISTORYSYNCTHROTTLE_H

#include <QMap>
#include <QPair>
#include <QString>

class ProtocolHistorySyncThrottle
{
public:
    static const qint64 MinimumIntervalMs = 10 * 60 * 1000;

    bool shouldRequest(const QString &streamId, const QString &conversationId,
        qint64 monotonicNowMs) const
    {
        if (streamId.isEmpty() || conversationId.isEmpty())
            return false;
        const auto it = FLastAcceptedRequest.constFind(qMakePair(streamId, conversationId));
        return it == FLastAcceptedRequest.constEnd() ||
            monotonicNowMs - it.value() >= MinimumIntervalMs;
    }

    void recordAccepted(const QString &streamId, const QString &conversationId,
        qint64 monotonicNowMs)
    {
        if (!streamId.isEmpty() && !conversationId.isEmpty())
            FLastAcceptedRequest.insert(qMakePair(streamId, conversationId), monotonicNowMs);
    }

private:
    QMap<QPair<QString, QString>, qint64> FLastAcceptedRequest;
};

#endif // PROTOCOLHISTORYSYNCTHROTTLE_H
