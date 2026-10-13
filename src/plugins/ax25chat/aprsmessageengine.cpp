#include "aprsmessageengine.h"

#include "aprsmessagecodec.h"
#include "aprsroomcodec.h"

#include <algorithm>

namespace
{
const QString kAprsAx25Destination = QStringLiteral("APRS");
const quint32 kMessageIdSpace = 60466176U; // 36^5

QString canonical(const QString &value)
{
    return value.trimmed().toUpper();
}
}

namespace AprsChat
{
AprsMessageEngine::AprsMessageEngine(Axcp::Transport *transport,
                                     const Config &config,
                                     MessageIdGenerator messageIdGenerator,
                                     QObject *parent)
    : QObject(parent),
      m_transport(transport),
      m_config(config),
      m_messageIdGenerator(std::move(messageIdGenerator))
{
    m_config.localCallsign = canonical(m_config.localCallsign);
    m_config.retryTimeoutMs = qMax(1, m_config.retryTimeoutMs);
    m_config.maxRetries = qMax(0, m_config.maxRetries);
    m_config.maximumReceivedIds = qMax(0, m_config.maximumReceivedIds);
    m_config.receivedIdTtlMs = qMax<qint64>(0, m_config.receivedIdTtlMs);

    if (m_transport) {
        m_transport->setReceiveHandler([this](const QString &source, const QByteArray &payload) {
            handleIncoming(source, payload, m_nowMs);
        });
    }
}

AprsMessageEngine::~AprsMessageEngine()
{
    if (m_transport)
        m_transport->setReceiveHandler(Axcp::Transport::ReceiveHandler());
}

bool AprsMessageEngine::sendMessage(const QString &destination,
                                    const QString &text,
                                    qint64 nowMs,
                                    QString *messageId,
                                    QString *error)
{
    if (error)
        error->clear();
    if (messageId)
        messageId->clear();
    m_nowMs = nowMs;

    if (!m_transport)
        return error ? (*error = QStringLiteral("APRS transport is not available"), false) : false;

    const QString peer = canonical(destination);
    if (peer.isEmpty() || peer == m_config.localCallsign)
        return error ? (*error = QStringLiteral("APRS destination must be a remote station"), false) : false;

    QString id;
    QString key;
    for (int attempt = 0; attempt < 6; ++attempt) {
        id = generateMessageId();
        AprsMessage::Message candidate;
        candidate.destination = peer;
        candidate.body = text;
        candidate.messageId = id;
        QByteArray ignored;
        QString codecError;
        if (!AprsMessage::Codec::encode(candidate, &ignored, &codecError)) {
            if (error)
                *error = codecError;
            return false;
        }
        key = pendingKey(peer, id);
        if (!m_pending.contains(key))
            break;
        id.clear();
    }
    if (id.isEmpty())
        return error ? (*error = QStringLiteral("could not allocate a unique APRS message ID"), false) : false;

    AprsMessage::Message aprsMessage;
    aprsMessage.destination = peer;
    aprsMessage.body = text;
    aprsMessage.messageId = id;
    QByteArray wirePayload;
    QString codecError;
    if (!AprsMessage::Codec::encode(aprsMessage, &wirePayload, &codecError)) {
        if (error)
            *error = codecError;
        return false;
    }

    PendingTransmission pending;
    pending.destination = peer;
    pending.messageId = id;
    pending.wirePayload = wirePayload;
    pending.deadlineMs = nowMs + m_config.retryTimeoutMs;
    m_pending.insert(key, pending);

    if (!m_transport->send(kAprsAx25Destination, wirePayload)) {
        m_pending.remove(key);
        if (error)
            *error = QStringLiteral("transport rejected APRS message");
        return false;
    }
    if (messageId)
        *messageId = id;
    return true;
}

bool AprsMessageEngine::sendGroupJoinCommand(const QString &groupName, qint64 nowMs,
                                             QString *messageId, QString *error)
{
    if (messageId)
        messageId->clear();
    if (error)
        error->clear();
    QByteArray command;
    if (!AprsRoom::Codec::encodeJoinCommand(groupName, &command, error))
        return false;
    return sendMessage(QStringLiteral("ANSRVR"), QString::fromLatin1(command), nowMs,
                       messageId, error);
}

bool AprsMessageEngine::sendGroupLeaveCommand(const QString &groupName, qint64 nowMs,
                                              QString *messageId, QString *error)
{
    if (messageId)
        messageId->clear();
    if (error)
        error->clear();
    QByteArray command;
    if (!AprsRoom::Codec::encodeLeaveCommand(groupName, &command, error))
        return false;
    return sendMessage(QStringLiteral("ANSRVR"), QString::fromLatin1(command), nowMs,
                       messageId, error);
}

bool AprsMessageEngine::sendGroupMessage(const QString &groupName, const QString &text,
                                         qint64 nowMs, QString *messageId, QString *error)
{
    if (messageId)
        messageId->clear();
    if (error)
        error->clear();
    AprsRoom::RoomMessage roomMessage;
    roomMessage.roomName = groupName;
    roomMessage.body = text;
    QByteArray command;
    if (!AprsRoom::Codec::encode(roomMessage, &command, error))
        return false;
    return sendMessage(QStringLiteral("ANSRVR"), QString::fromUtf8(command), nowMs,
                       messageId, error);
}

void AprsMessageEngine::handleIncoming(const QString &source,
                                       const QByteArray &payload,
                                       qint64 nowMs)
{
    m_nowMs = nowMs;
    if (!m_transport)
        return;

    AprsMessage::Message message;
    if (!AprsMessage::Codec::decode(payload, &message) ||
        canonical(message.destination) != m_config.localCallsign)
        return;

    const QString sender = canonical(source);
    if (sender.isEmpty() || sender == m_config.localCallsign)
        return;

    if (message.isAck) {
        processAck(sender, message.messageId);
        return;
    }

    if (!message.messageId.isEmpty())
        sendAck(sender, message.messageId);

    pruneReceived(nowMs);
    if (!message.messageId.isEmpty()) {
        const QString key = receivedKey(sender, message.messageId);
        if (m_receivedIds.contains(key)) {
            m_receivedIds[key] = nowMs;
            return;
        }
        rememberReceived(key, nowMs);
    }
    emit incomingMessage(sender, message.messageId, message.body);
}

void AprsMessageEngine::tick(qint64 nowMs)
{
    m_nowMs = nowMs;
    QList<QString> due;
    for (auto it = m_pending.cbegin(); it != m_pending.cend(); ++it) {
        if (nowMs >= it->deadlineMs)
            due.append(it.key());
    }

    for (const QString &key : due) {
        auto it = m_pending.find(key);
        if (it == m_pending.end())
            continue;
        if (it->retries >= m_config.maxRetries) {
            const QString id = it->messageId;
            m_pending.erase(it);
            emit messageStatusChanged(id, false,
                QStringLiteral("APRS acknowledgement timed out"));
            continue;
        }

        ++it->retries;
        it->deadlineMs = nowMs + m_config.retryTimeoutMs;
        const QString id = it->messageId;
        const QByteArray wirePayload = it->wirePayload;
        if (!m_transport || !m_transport->send(kAprsAx25Destination, wirePayload)) {
            auto failed = m_pending.find(key);
            if (failed != m_pending.end()) {
                m_pending.erase(failed);
                emit messageStatusChanged(id, false,
                    QStringLiteral("transport rejected APRS retry"));
            }
        }
    }
    pruneReceived(nowMs);
}

QString AprsMessageEngine::generateMessageId()
{
    if (m_messageIdGenerator)
        return m_messageIdGenerator();
    const quint32 id = m_nextMessageId;
    m_nextMessageId = (m_nextMessageId + 1) % kMessageIdSpace;
    return QString::number(id, 36).toUpper();
}

QString AprsMessageEngine::pendingKey(const QString &peer, const QString &messageId) const
{
    return canonical(peer) + QChar(0x1f) + messageId;
}

QString AprsMessageEngine::receivedKey(const QString &peer, const QString &messageId) const
{
    return canonical(peer) + QChar(0x1f) + messageId;
}

void AprsMessageEngine::sendAck(const QString &destination, const QString &messageId)
{
    AprsMessage::Message ack;
    ack.isAck = true;
    ack.destination = canonical(destination);
    ack.messageId = messageId;
    QByteArray wirePayload;
    if (AprsMessage::Codec::encode(ack, &wirePayload))
        m_transport->send(kAprsAx25Destination, wirePayload);
}

void AprsMessageEngine::processAck(const QString &source, const QString &messageId)
{
    const QString key = pendingKey(source, messageId);
    auto it = m_pending.find(key);
    if (it == m_pending.end())
        return;
    const QString id = it->messageId;
    m_pending.erase(it);
    emit messageStatusChanged(id, true, QString());
}

void AprsMessageEngine::pruneReceived(qint64 nowMs)
{
    if (m_config.receivedIdTtlMs <= 0 || m_config.maximumReceivedIds <= 0) {
        m_receivedIds.clear();
        return;
    }
    for (auto it = m_receivedIds.begin(); it != m_receivedIds.end();) {
        if (nowMs - it.value() >= m_config.receivedIdTtlMs)
            it = m_receivedIds.erase(it);
        else
            ++it;
    }
    while (m_receivedIds.size() > m_config.maximumReceivedIds) {
        auto oldest = m_receivedIds.begin();
        for (auto it = m_receivedIds.begin(); it != m_receivedIds.end(); ++it) {
            if (it.value() < oldest.value())
                oldest = it;
        }
        m_receivedIds.erase(oldest);
    }
}

void AprsMessageEngine::rememberReceived(const QString &key, qint64 nowMs)
{
    if (m_config.maximumReceivedIds <= 0 || m_config.receivedIdTtlMs <= 0)
        return;
    m_receivedIds.insert(key, nowMs);
    pruneReceived(nowMs);
}
}
