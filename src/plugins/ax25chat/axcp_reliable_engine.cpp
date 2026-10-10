#include "axcp_reliable_engine.h"
#include "axcpcodec.h"

#include <QList>
#include <QRandomGenerator>

#include <utility>

namespace Axcp
{
namespace
{
void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

bool splitUtf8(const QByteArray &utf8, int maximumBytes, QList<QByteArray> *parts)
{
    if (!parts || maximumBytes <= 0)
        return false;
    parts->clear();
    if (utf8.isEmpty()) {
        parts->append(QByteArray());
        return true;
    }

    int start = 0;
    while (start < utf8.size()) {
        int end = qMin(start + maximumBytes, utf8.size());
        if (end < utf8.size()) {
            while (end > start &&
                (static_cast<unsigned char>(utf8.at(end)) & 0xc0) == 0x80)
                --end;
            if (end == start)
                return false;
        }
        parts->append(utf8.mid(start, end - start));
        start = end;
    }
    return true;
}
}

ReliableEngine::ReliableEngine(Transport *transport, const ReliableConfig &config,
    MessageIdGenerator messageIdGenerator, QObject *parent)
    : QObject(parent),
      m_transport(transport),
      m_config(config),
      m_messageIdGenerator(std::move(messageIdGenerator))
{
    if (m_config.ackTimeoutMs <= 0)
        m_config.ackTimeoutMs = 5000;
    if (m_config.maxRetries < 0)
        m_config.maxRetries = 0;
    if (m_config.maximumPayloadBytes <= 0 ||
        m_config.maximumPayloadBytes > Codec::DefaultMaximumPayloadBytes)
        m_config.maximumPayloadBytes = Codec::DefaultMaximumPayloadBytes;
    if (m_config.maximumReceivedIds <= 0)
        m_config.maximumReceivedIds = 1000;
    if (m_config.receivedIdTtlMs <= 0)
        m_config.receivedIdTtlMs = 24LL * 60 * 60 * 1000;
    if (m_config.maximumReassemblies <= 0)
        m_config.maximumReassemblies = 64;
    if (m_config.reassemblyTtlMs <= 0)
        m_config.reassemblyTtlMs = 10LL * 60 * 1000;

    if (!m_messageIdGenerator) {
        m_messageIdGenerator = []() {
            return QRandomGenerator::system()->generate();
        };
    }

    if (m_transport) {
        m_transport->setReceiveHandler([this](const QString &source, const QByteArray &payload) {
            handleIncoming(source, payload, m_nowMs);
        });
    }
}

ReliableEngine::~ReliableEngine()
{
    if (m_transport)
        m_transport->setReceiveHandler(Transport::ReceiveHandler());
}

bool ReliableEngine::sendMessage(const QString &destination, const QString &text, qint64 nowMs,
    quint32 *messageId, QString *error)
{
    m_nowMs = nowMs;
    setError(error, QString());
    if (messageId)
        *messageId = 0;

    if (!m_transport) {
        setError(error, QStringLiteral("No AXCP transport is available"));
        return false;
    }
    if (destination.trimmed().isEmpty()) {
        setError(error, QStringLiteral("Destination must not be empty"));
        return false;
    }

    QList<QByteArray> parts;
    if (!splitUtf8(text.toUtf8(), m_config.maximumPayloadBytes, &parts)) {
        setError(error, QStringLiteral("Payload limit cannot hold the next UTF-8 code point"));
        return false;
    }
    if (parts.size() > 255) {
        setError(error, QStringLiteral("Message exceeds the 255-fragment AXCP limit"));
        return false;
    }

    quint32 id = generateMessageId();
    for (int attempt = 0; attempt < 16 && m_outgoing.contains(id); ++attempt)
        id = generateMessageId();
    if (m_outgoing.contains(id)) {
        setError(error, QStringLiteral("Could not generate a unique message ID"));
        return false;
    }

    QList<PendingTransmission> transmissions;
    for (int i = 0; i < parts.size(); ++i) {
        const quint16 sequence = m_nextSequenceByPeer.value(destination, 0);
        m_nextSequenceByPeer.insert(destination, static_cast<quint16>(sequence + 1));

        Packet packet;
        packet.type = Type::Message;
        packet.flags = Codec::AckRequiredFlag;
        packet.messageId = id;
        packet.sequence = sequence;
        packet.payload = parts.at(i);
        if (parts.size() > 1) {
            packet.flags |= Codec::FragmentedFlag;
            packet.fragmentIndex = static_cast<quint8>(i);
            packet.fragmentCount = static_cast<quint8>(parts.size());
            if (i == parts.size() - 1)
                packet.flags |= Codec::FinalFragmentFlag;
        }

        PendingTransmission pending;
        pending.destination = destination;
        pending.messageId = id;
        pending.sequence = sequence;
        pending.deadlineMs = nowMs + m_config.ackTimeoutMs;
        QString encodeError;
        if (!Codec::encode(packet, &pending.wirePayload, &encodeError,
                m_config.maximumPayloadBytes)) {
            setError(error, encodeError);
            return false;
        }
        transmissions.append(pending);
    }

    OutgoingMessage outgoing;
    outgoing.destination = destination;
    outgoing.pendingFragments = transmissions.size();
    m_outgoing.insert(id, outgoing);
    for (const PendingTransmission &pending : transmissions)
        m_pending.insert(pendingKey(destination, id, pending.sequence), pending);

    if (messageId)
        *messageId = id;
    for (const PendingTransmission &pending : transmissions) {
        if (!m_transport->send(destination, pending.wirePayload)) {
            const QString reason = QStringLiteral("AXCP transport rejected the message");
            failMessage(id, reason);
            setError(error, reason);
            return false;
        }
    }
    return true;
}

void ReliableEngine::tick(qint64 nowMs)
{
    m_nowMs = nowMs;
    pruneReassemblies(nowMs);
    pruneReceived(nowMs);

    QList<QString> expired;
    for (auto it = m_pending.cbegin(); it != m_pending.cend(); ++it) {
        if (nowMs >= it->deadlineMs)
            expired.append(it.key());
    }

    for (const QString &key : expired) {
        auto pending = m_pending.find(key);
        if (pending == m_pending.end())
            continue;
        const quint32 messageId = pending->messageId;
        if (pending->retries >= m_config.maxRetries) {
            failMessage(messageId, QStringLiteral("AXCP ACK timeout; retry limit exhausted"));
            continue;
        }

        ++pending->retries;
        pending->deadlineMs = nowMs + m_config.ackTimeoutMs;
        const QString destination = pending->destination;
        const QByteArray wirePayload = pending->wirePayload;
        if (!m_transport || !m_transport->send(destination, wirePayload))
            failMessage(messageId, QStringLiteral("AXCP transport rejected a retry"));
    }
}

void ReliableEngine::handleIncoming(const QString &source, const QByteArray &payload, qint64 nowMs)
{
    m_nowMs = nowMs;
    pruneReassemblies(nowMs);
    pruneReceived(nowMs);
    if (!m_transport || source.trimmed().isEmpty())
        return;

    Packet packet;
    QString decodeError;
    if (!Codec::decode(payload, &packet, &decodeError, m_config.maximumPayloadBytes))
        return;

    if (packet.type == Type::Ack) {
        processAck(source, packet.messageId, packet.sequence);
        return;
    }
    if (packet.type != Type::Message)
        return;

    const bool ackRequired = packet.flags & Codec::AckRequiredFlag;
    const QString messageKey = receivedKey(source, packet.messageId);
    if (m_receivedIds.contains(messageKey)) {
        if (ackRequired)
            sendAck(source, packet.messageId, packet.sequence);
        return;
    }

    if (packet.flags & Codec::FragmentedFlag) {
        auto reassembly = m_reassemblies.find(messageKey);
        if (reassembly == m_reassemblies.end()) {
            while (m_reassemblies.size() >= m_config.maximumReassemblies) {
                auto oldest = m_reassemblies.begin();
                for (auto it = m_reassemblies.begin(); it != m_reassemblies.end(); ++it) {
                    if (it->lastUpdatedMs < oldest->lastUpdatedMs)
                        oldest = it;
                }
                m_reassemblies.erase(oldest);
            }
            Reassembly entry;
            entry.fragmentCount = packet.fragmentCount;
            entry.lastUpdatedMs = nowMs;
            reassembly = m_reassemblies.insert(messageKey, entry);
        } else if (reassembly->fragmentCount != packet.fragmentCount) {
            return;
        }

        auto fragment = reassembly->fragments.find(packet.fragmentIndex);
        if (fragment != reassembly->fragments.end()) {
            if (fragment.value() == packet.payload && ackRequired)
                sendAck(source, packet.messageId, packet.sequence);
            return;
        }

        reassembly->fragments.insert(packet.fragmentIndex, packet.payload);
        reassembly->lastUpdatedMs = nowMs;
        if (ackRequired)
            sendAck(source, packet.messageId, packet.sequence);
        if (reassembly->fragments.size() != reassembly->fragmentCount)
            return;

        QByteArray assembled;
        for (int index = 0; index < reassembly->fragmentCount; ++index) {
            const auto part = reassembly->fragments.constFind(static_cast<quint8>(index));
            if (part == reassembly->fragments.cend())
                return;
            assembled.append(part.value());
        }
        rememberReceived(messageKey, nowMs);
        m_reassemblies.erase(reassembly);
        emit incomingMessage(source, packet.messageId,
            QString::fromUtf8(assembled.constData(), assembled.size()));
        return;
    }

    rememberReceived(messageKey, nowMs);
    if (ackRequired)
        sendAck(source, packet.messageId, packet.sequence);

    const QString text = QString::fromUtf8(packet.payload.constData(), packet.payload.size());
    emit incomingMessage(source, packet.messageId, text);
}

void ReliableEngine::sendAck(const QString &destination, quint32 messageId, quint16 sequence)
{
    if (!m_transport)
        return;

    Packet ack;
    ack.type = Type::Ack;
    ack.messageId = messageId;
    ack.sequence = sequence;

    QByteArray wirePayload;
    if (Codec::encode(ack, &wirePayload, nullptr, m_config.maximumPayloadBytes))
        m_transport->send(destination, wirePayload);
}

void ReliableEngine::processAck(const QString &source, quint32 messageId, quint16 sequence)
{
    const QString key = pendingKey(source, messageId, sequence);
    auto pending = m_pending.find(key);
    if (pending == m_pending.end() || pending->destination != source)
        return;

    m_pending.erase(pending);
    auto outgoing = m_outgoing.find(messageId);
    if (outgoing == m_outgoing.end() || outgoing->destination != source)
        return;

    --outgoing->pendingFragments;
    if (outgoing->pendingFragments == 0) {
        m_outgoing.erase(outgoing);
        emit messageStatusChanged(messageId, true, QString());
    }
}

void ReliableEngine::failMessage(quint32 messageId, const QString &reason)
{
    if (m_outgoing.remove(messageId) == 0)
        return;

    for (auto pending = m_pending.begin(); pending != m_pending.end();) {
        if (pending->messageId == messageId)
            pending = m_pending.erase(pending);
        else
            ++pending;
    }
    emit messageStatusChanged(messageId, false, reason);
}

void ReliableEngine::pruneReassemblies(qint64 nowMs)
{
    for (auto it = m_reassemblies.begin(); it != m_reassemblies.end();) {
        if (nowMs >= it->lastUpdatedMs && nowMs - it->lastUpdatedMs >= m_config.reassemblyTtlMs)
            it = m_reassemblies.erase(it);
        else
            ++it;
    }
}

void ReliableEngine::pruneReceived(qint64 nowMs)
{
    for (auto it = m_receivedIds.begin(); it != m_receivedIds.end();) {
        if (nowMs >= it.value() && nowMs - it.value() >= m_config.receivedIdTtlMs)
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

void ReliableEngine::rememberReceived(const QString &key, qint64 nowMs)
{
    m_receivedIds.insert(key, nowMs);
    pruneReceived(nowMs);
}

quint32 ReliableEngine::generateMessageId() const
{
    return m_messageIdGenerator ? m_messageIdGenerator() : QRandomGenerator::system()->generate();
}

QString ReliableEngine::pendingKey(const QString &peer, quint32 messageId, quint16 sequence) const
{
    return peer + QLatin1Char('\x1f') + QString::number(messageId) + QLatin1Char('\x1f') +
        QString::number(sequence);
}

QString ReliableEngine::receivedKey(const QString &peer, quint32 messageId) const
{
    return peer + QLatin1Char('\x1f') + QString::number(messageId);
}
}
