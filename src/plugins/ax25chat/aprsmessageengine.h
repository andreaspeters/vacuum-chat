#ifndef APRSMESSAGEENGINE_H
#define APRSMESSAGEENGINE_H

#include "ax25transport.h"

#include <QByteArray>
#include <QMap>
#include <QObject>
#include <QString>

#include <functional>

namespace AprsChat
{
class AprsMessageEngine : public QObject
{
    Q_OBJECT

public:
    struct Config
    {
        QString localCallsign;
        int retryTimeoutMs = 120000;
        int maxRetries = 11;
        int maximumReceivedIds = 1000;
        qint64 receivedIdTtlMs = 24LL * 60 * 60 * 1000;
    };

    using MessageIdGenerator = std::function<QString()>;

    explicit AprsMessageEngine(Axcp::Transport *transport,
                               const Config &config,
                               MessageIdGenerator messageIdGenerator = MessageIdGenerator(),
                               QObject *parent = nullptr);
    ~AprsMessageEngine() override;

    bool sendMessage(const QString &destination, const QString &text, qint64 nowMs,
                     QString *messageId = nullptr, QString *error = nullptr);
    bool sendGroupJoinCommand(const QString &groupName, qint64 nowMs,
                              QString *messageId = nullptr, QString *error = nullptr);
    bool sendGroupLeaveCommand(const QString &groupName, qint64 nowMs,
                               QString *messageId = nullptr, QString *error = nullptr);
    bool sendGroupMessage(const QString &groupName, const QString &text, qint64 nowMs,
                          QString *messageId = nullptr, QString *error = nullptr);
    void handleIncoming(const QString &source, const QByteArray &payload, qint64 nowMs);
    void tick(qint64 nowMs);

signals:
    void incomingMessage(const QString &source, const QString &messageId, const QString &text);
    void messageStatusChanged(const QString &messageId, bool delivered, const QString &error);

private:
    struct PendingTransmission
    {
        QString destination;
        QString messageId;
        QByteArray wirePayload;
        int retries = 0;
        qint64 deadlineMs = 0;
    };

    QString generateMessageId();
    QString pendingKey(const QString &peer, const QString &messageId) const;
    QString receivedKey(const QString &peer, const QString &messageId) const;
    void sendAck(const QString &destination, const QString &messageId);
    void processAck(const QString &source, const QString &messageId);
    void pruneReceived(qint64 nowMs);
    void rememberReceived(const QString &key, qint64 nowMs);

    Axcp::Transport *m_transport = nullptr;
    Config m_config;
    MessageIdGenerator m_messageIdGenerator;
    QMap<QString, PendingTransmission> m_pending;
    QMap<QString, qint64> m_receivedIds;
    quint32 m_nextMessageId = 0;
    qint64 m_nowMs = 0;
};
}

#endif // APRSMESSAGEENGINE_H
