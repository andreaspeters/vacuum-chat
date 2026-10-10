#ifndef AXCPRELIABLEENGINE_H
#define AXCPRELIABLEENGINE_H

#include <QObject>
#include <QByteArray>
#include <QMap>
#include <QString>
#include <QtGlobal>

#include <functional>

namespace Axcp
{
class Transport
{
public:
    using ReceiveHandler = std::function<void(const QString &, const QByteArray &)>;

    virtual ~Transport() {}
    virtual bool send(const QString &destination, const QByteArray &payload) = 0;
    virtual void setReceiveHandler(ReceiveHandler handler) = 0;
};

struct ReliableConfig
{
    int ackTimeoutMs = 5000;
    int maxRetries = 3;
    int maximumPayloadBytes = 180;
    int maximumReceivedIds = 1000;
    qint64 receivedIdTtlMs = 24LL * 60 * 60 * 1000;
    int maximumReassemblies = 64;
    qint64 reassemblyTtlMs = 10LL * 60 * 1000;
};

class ReliableEngine : public QObject
{
    Q_OBJECT

public:
    using MessageIdGenerator = std::function<quint32()>;

    explicit ReliableEngine(Transport *transport,
        const ReliableConfig &config = ReliableConfig(),
        MessageIdGenerator messageIdGenerator = MessageIdGenerator(),
        QObject *parent = nullptr);
    ~ReliableEngine() override;

    bool sendMessage(const QString &destination, const QString &text, qint64 nowMs,
        quint32 *messageId = nullptr, QString *error = nullptr);
    void handleIncoming(const QString &source, const QByteArray &payload, qint64 nowMs);
    void tick(qint64 nowMs);

signals:
    void incomingMessage(const QString &source, quint32 messageId, const QString &text);
    void messageStatusChanged(quint32 messageId, bool delivered, const QString &error);

private:
    struct PendingTransmission
    {
        QString destination;
        quint32 messageId = 0;
        quint16 sequence = 0;
        QByteArray wirePayload;
        int retries = 0;
        qint64 deadlineMs = 0;
    };

    struct OutgoingMessage
    {
        QString destination;
        int pendingFragments = 0;
    };

    struct Reassembly
    {
        quint8 fragmentCount = 0;
        qint64 lastUpdatedMs = 0;
        QMap<quint8, QByteArray> fragments;
    };

    void sendAck(const QString &destination, quint32 messageId, quint16 sequence);
    void processAck(const QString &source, quint32 messageId, quint16 sequence);
    void failMessage(quint32 messageId, const QString &reason);
    void pruneReassemblies(qint64 nowMs);
    void pruneReceived(qint64 nowMs);
    void rememberReceived(const QString &key, qint64 nowMs);
    quint32 generateMessageId() const;
    QString pendingKey(const QString &peer, quint32 messageId, quint16 sequence) const;
    QString receivedKey(const QString &peer, quint32 messageId) const;

    Transport *m_transport = nullptr;
    ReliableConfig m_config;
    MessageIdGenerator m_messageIdGenerator;
    QMap<QString, quint16> m_nextSequenceByPeer;
    QMap<QString, PendingTransmission> m_pending;
    QMap<quint32, OutgoingMessage> m_outgoing;
    QMap<QString, Reassembly> m_reassemblies;
    QMap<QString, qint64> m_receivedIds;
    qint64 m_nowMs = 0;
};
}

#endif // AXCPRELIABLEENGINE_H
