#include "axcp_reliable_engine.h"
#include "axcpcodec.h"

#include <QCoreApplication>
#include <iostream>
#include <type_traits>
#include <utility>

namespace
{
class FakeBus;

class FakeTransport final : public Axcp::Transport
{
public:
    FakeTransport(const QString &station, FakeBus *bus);
    bool send(const QString &destination, const QByteArray &payload) override;
    void setReceiveHandler(ReceiveHandler handler) override { m_handler = handler; }
    void receive(const QString &source, const QByteArray &payload)
    {
        if (m_handler)
            m_handler(source, payload);
    }

    QString station;
    QList<QByteArray> sentFrames;

private:
    FakeBus *m_bus;
    ReceiveHandler m_handler;
};

class FakeBus
{
public:
    void add(FakeTransport *transport) { stations.insert(transport->station, transport); }
    bool transmit(const QString &source, const QString &destination, const QByteArray &payload)
    {
        Axcp::Packet packet;
        if (!Axcp::Codec::decode(payload, &packet))
            return false;
        if (packet.type == Axcp::Type::Message && dropMessages)
            return true;
        if (packet.type == Axcp::Type::Ack && acknowledgementsToDrop > 0) {
            --acknowledgementsToDrop;
            return true;
        }
        FakeTransport *recipient = stations.value(destination, nullptr);
        if (!recipient)
            return false;
        recipient->receive(source, payload);
        return true;
    }

    QMap<QString, FakeTransport *> stations;
    bool dropMessages = false;
    int acknowledgementsToDrop = 0;
};

FakeTransport::FakeTransport(const QString &id, FakeBus *bus)
    : station(id), m_bus(bus)
{
    m_bus->add(this);
}

bool FakeTransport::send(const QString &destination, const QByteArray &payload)
{
    sentFrames.append(payload);
    return m_bus->transmit(station, destination, payload);
}

Axcp::ReliableEngine::MessageIdGenerator fixedMessageId(quint32 id)
{
    return [id]() { return id; };
}

bool check(bool condition, const char *message)
{
    if (!condition)
        std::cerr << message << '\n';
    return condition;
}
}

bool testDuplicateIsAcknowledgedButNotRedelivered()
{
    FakeBus bus;
    FakeTransport transportA(QStringLiteral("A"), &bus);
    FakeTransport transportB(QStringLiteral("B"), &bus);
    Axcp::ReliableEngine receiver(&transportB);
    Axcp::Packet packet;
    packet.type = Axcp::Type::Message;
    packet.flags = Axcp::Codec::AckRequiredFlag;
    packet.messageId = 42;
    packet.sequence = 9;
    packet.payload = QByteArrayLiteral("duplicate");
    QByteArray wire;
    QString error;
    if (!Axcp::Codec::encode(packet, &wire, &error))
        return check(false, "synthetic duplicate packet did not encode");

    int receivedCount = 0;
    QObject::connect(&receiver, &Axcp::ReliableEngine::incomingMessage, &receiver,
        [&receivedCount](const QString &, quint32, const QString &) { ++receivedCount; });
    receiver.handleIncoming(QStringLiteral("A"), wire, 0);
    receiver.handleIncoming(QStringLiteral("A"), wire, 1);

    int ackCount = 0;
    for (const QByteArray &frame : transportB.sentFrames) {
        Axcp::Packet ack;
        if (Axcp::Codec::decode(frame, &ack) && ack.type == Axcp::Type::Ack &&
            ack.messageId == packet.messageId && ack.sequence == packet.sequence)
            ++ackCount;
    }
    return check(receivedCount == 1, "duplicate message was delivered more than once") &&
        check(ackCount == 2, "duplicate message was not acknowledged again");
}

bool testDuplicateCacheIsBoundedAndExpires()
{
    FakeBus bus;
    FakeTransport transportA(QStringLiteral("A"), &bus);
    FakeTransport transportB(QStringLiteral("B"), &bus);
    Axcp::ReliableConfig config;
    config.maximumReceivedIds = 1;
    config.receivedIdTtlMs = 10;
    Axcp::ReliableEngine receiver(&transportB, config);
    int receivedCount = 0;
    QObject::connect(&receiver, &Axcp::ReliableEngine::incomingMessage, &receiver,
        [&receivedCount](const QString &, quint32, const QString &) { ++receivedCount; });

    Axcp::Packet first;
    first.type = Axcp::Type::Message;
    first.messageId = 1;
    first.sequence = 1;
    first.payload = QByteArrayLiteral("first");
    Axcp::Packet second = first;
    second.messageId = 2;
    QByteArray firstWire;
    QByteArray secondWire;
    if (!Axcp::Codec::encode(first, &firstWire) ||
        !Axcp::Codec::encode(second, &secondWire))
        return check(false, "synthetic cache packets did not encode");

    receiver.handleIncoming(QStringLiteral("A"), firstWire, 0);
    receiver.handleIncoming(QStringLiteral("A"), firstWire, 1);
    if (!check(receivedCount == 1, "recent message ID was not suppressed"))
        return false;
    receiver.handleIncoming(QStringLiteral("A"), secondWire, 2);
    receiver.handleIncoming(QStringLiteral("A"), firstWire, 3);
    if (!check(receivedCount == 3, "duplicate cache did not enforce its configured size"))
        return false;
    receiver.handleIncoming(QStringLiteral("A"), firstWire, 12);
    if (!check(receivedCount == 3, "cache entry expired before its TTL"))
        return false;
    receiver.handleIncoming(QStringLiteral("A"), firstWire, 13);
    return check(receivedCount == 4, "cache entry was not evicted at its TTL");
}

bool testUtf8FragmentationAndReassembly()
{
    FakeBus bus;
    FakeTransport transportA(QStringLiteral("A"), &bus);
    FakeTransport transportB(QStringLiteral("B"), &bus);
    Axcp::ReliableConfig config;
    config.maximumPayloadBytes = 4;
    Axcp::ReliableEngine sender(&transportA, config, fixedMessageId(17));
    Axcp::ReliableEngine receiver(&transportB, config);

    const QString original = QString::fromUtf8("a\xf0\x9f\x99\x82" "b");
    QString receivedText;
    int receivedCount = 0;
    bool delivered = false;
    QObject::connect(&receiver, &Axcp::ReliableEngine::incomingMessage, &receiver,
        [&receivedText, &receivedCount](const QString &, quint32, const QString &text) {
            receivedText = text;
            ++receivedCount;
        });
    QObject::connect(&sender, &Axcp::ReliableEngine::messageStatusChanged, &sender,
        [&delivered](quint32, bool ok, const QString &) { delivered = ok; });

    QString error;
    if (!check(sender.sendMessage(QStringLiteral("B"), original, 0, nullptr, &error),
            "UTF-8 message was rejected instead of fragmented"))
        return false;
    if (!check(receivedCount == 1 && receivedText == original,
            "UTF-8 fragments were not reassembled exactly once"))
        return false;
    if (!check(delivered && transportA.sentFrames.size() == 3,
            "each fragment was not acknowledged"))
        return false;

    for (int i = 0; i < transportA.sentFrames.size(); ++i) {
        Axcp::Packet packet;
        if (!check(Axcp::Codec::decode(transportA.sentFrames.at(i), &packet, &error, 4),
                "fragment payload was not valid UTF-8"))
            return false;
        if (!check(packet.fragmentIndex == i && packet.fragmentCount == 3,
                "fragment index/count is incorrect"))
            return false;
    }
    return true;
}

template <typename T, typename = void>
struct HasDeterministicTick : std::false_type {};

template <typename T>
struct HasDeterministicTick<T,
    decltype(void(std::declval<T &>().tick(qint64())))> : std::true_type {};

static_assert(HasDeterministicTick<Axcp::ReliableEngine>::value,
    "ReliableEngine must expose deterministic time advancement for ACK retries");

bool testRetryAndExhaustion()
{
    FakeBus bus;
    bus.dropMessages = true;
    FakeTransport transport(QStringLiteral("A"), &bus);
    Axcp::ReliableConfig config;
    config.ackTimeoutMs = 5000;
    config.maxRetries = 3;
    Axcp::ReliableEngine sender(&transport, config, fixedMessageId(99));
    int failures = 0;
    QObject::connect(&sender, &Axcp::ReliableEngine::messageStatusChanged, &sender,
        [&failures](quint32, bool delivered, const QString &) {
            if (!delivered)
                ++failures;
        });

    QString error;
    if (!check(sender.sendMessage(QStringLiteral("B"), QStringLiteral("lost"), 0,
            nullptr, &error), "transport did not accept the initial transmission"))
        return false;
    sender.tick(4999);
    if (!check(transport.sentFrames.size() == 1, "retry occurred before ACK timeout"))
        return false;
    sender.tick(5000);
    sender.tick(10000);
    sender.tick(15000);
    if (!check(transport.sentFrames.size() == 4,
            "engine did not send the initial frame plus three retries"))
        return false;
    sender.tick(20000);
    sender.tick(25000);
    return check(failures == 1, "delivery failure was not reported after retry exhaustion") &&
        check(transport.sentFrames.size() == 4, "transmission continued after retry exhaustion");
}

bool testFragmentRetryAndDuplicateSuppression()
{
    FakeBus bus;
    bus.acknowledgementsToDrop = 1;
    FakeTransport transportA(QStringLiteral("A"), &bus);
    FakeTransport transportB(QStringLiteral("B"), &bus);
    Axcp::ReliableConfig config;
    config.maximumPayloadBytes = 4;
    config.ackTimeoutMs = 100;
    config.maxRetries = 1;
    Axcp::ReliableEngine sender(&transportA, config, fixedMessageId(31));
    Axcp::ReliableEngine receiver(&transportB, config);

    int receivedCount = 0;
    bool delivered = false;
    QObject::connect(&receiver, &Axcp::ReliableEngine::incomingMessage, &receiver,
        [&receivedCount](const QString &, quint32, const QString &) { ++receivedCount; });
    QObject::connect(&sender, &Axcp::ReliableEngine::messageStatusChanged, &sender,
        [&delivered](quint32, bool ok, const QString &) { delivered = ok; });

    QString error;
    if (!check(sender.sendMessage(QStringLiteral("B"),
            QString::fromUtf8("a\xf0\x9f\x99\x82" "b"), 0, nullptr, &error),
            "fragmented message was rejected"))
        return false;
    if (!check(receivedCount == 1 && !delivered && transportA.sentFrames.size() == 3,
            "dropped fragment ACK did not leave exactly one fragment pending"))
        return false;

    sender.tick(99);
    if (!check(transportA.sentFrames.size() == 3, "fragment retried before its timeout"))
        return false;
    sender.tick(100);
    if (!check(transportA.sentFrames.size() == 4 && delivered && receivedCount == 1,
            "missing fragment was not retried and deduplicated at the receiver"))
        return false;

    Axcp::Packet first;
    Axcp::Packet retry;
    return check(Axcp::Codec::decode(transportA.sentFrames.first(), &first) &&
            Axcp::Codec::decode(transportA.sentFrames.last(), &retry) &&
            first.type == Axcp::Type::Message && retry.type == Axcp::Type::Message &&
            first.messageId == retry.messageId && first.sequence == retry.sequence &&
            first.fragmentIndex == retry.fragmentIndex,
            "retry did not resend the exact unacknowledged fragment");
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    FakeBus bus;
    FakeTransport transportA(QStringLiteral("A"), &bus);
    FakeTransport transportB(QStringLiteral("B"), &bus);
    Axcp::ReliableEngine sender(&transportA, Axcp::ReliableConfig(), fixedMessageId(0x12345678));
    Axcp::ReliableEngine receiver(&transportB);

    int receivedCount = 0;
    QString receivedText;
    quint32 receivedId = 0;
    bool delivered = false;
    quint32 deliveredId = 0;
    QObject::connect(&receiver, &Axcp::ReliableEngine::incomingMessage, &receiver,
        [&receivedCount, &receivedText, &receivedId](const QString &, quint32 id, const QString &text) {
            ++receivedCount;
            receivedText = text;
            receivedId = id;
        });
    QObject::connect(&sender, &Axcp::ReliableEngine::messageStatusChanged, &sender,
        [&delivered, &deliveredId](quint32 id, bool wasDelivered, const QString &) {
            delivered = wasDelivered;
            deliveredId = id;
        });

    quint32 messageId = 0;
    QString error;
    if (!check(sender.sendMessage(QStringLiteral("B"), QStringLiteral("Hello, QRV?"),
            0, &messageId, &error), "sender rejected a valid direct message"))
        return 1;
    if (!check(messageId == 0x12345678, "message ID was not returned"))
        return 1;
    if (!check(receivedCount == 1 && receivedText == QStringLiteral("Hello, QRV?") &&
            receivedId == messageId, "receiver did not deliver the message once with its ID"))
        return 1;
    if (!check(delivered && deliveredId == messageId,
            "sender did not report delivery after matching ACK"))
        return 1;
    if (!check(transportB.sentFrames.size() == 1, "receiver did not send one ACK"))
        return 1;
    Axcp::Packet ack;
    if (!check(Axcp::Codec::decode(transportB.sentFrames.first(), &ack) &&
            ack.type == Axcp::Type::Ack && ack.messageId == messageId && ack.sequence == 0,
            "ACK did not match the received message ID and sequence"))
        return 1;
    if (!testUtf8FragmentationAndReassembly())
        return 1;
    if (!testDuplicateIsAcknowledgedButNotRedelivered())
        return 1;
    if (!testDuplicateCacheIsBoundedAndExpires())
        return 1;
    if (!testRetryAndExhaustion())
        return 1;
    if (!testFragmentRetryAndDuplicateSuppression())
        return 1;
    return 0;
}
