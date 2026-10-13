#include "aprsmessageengine.h"
#include "aprsmessagecodec.h"

#include <iostream>
#include <utility>

namespace
{
class FakeTransport final : public Axcp::Transport
{
public:
    bool send(const QString &destination, const QByteArray &payload) override
    {
        destinations.append(destination);
        payloads.append(payload);
        return acceptWrites;
    }
    void setReceiveHandler(ReceiveHandler handler) override { receiveHandler = std::move(handler); }
    void receive(const QString &source, const QByteArray &payload)
    {
        if (receiveHandler)
            receiveHandler(source, payload);
    }

    QStringList destinations;
    QList<QByteArray> payloads;
    ReceiveHandler receiveHandler;
    bool acceptWrites = true;
};

bool check(bool condition, const char *message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

AprsMessage::Message message(QString destination, QString body, QString id = {})
{
    AprsMessage::Message result;
    result.destination = std::move(destination);
    result.body = std::move(body);
    result.messageId = std::move(id);
    return result;
}

bool testSendsStandardAprsMessageToAprsDestination()
{
    FakeTransport transport;
    AprsChat::AprsMessageEngine::Config config;
    config.localCallsign = QStringLiteral("N0CALL");
    AprsChat::AprsMessageEngine engine(&transport, config, [] { return QStringLiteral("A12"); });
    QString messageId;
    QString error;
    if (!engine.sendMessage(QStringLiteral("DL1BBB-9"), QStringLiteral("hello APRS"),
                            10, &messageId, &error)) {
        std::cerr << error.toStdString() << '\n';
        return false;
    }
    AprsMessage::Message decoded;
    return check(messageId == QStringLiteral("A12") && transport.destinations.size() == 1 &&
                 transport.destinations.first() == QStringLiteral("APRS"),
                 "radio frame must use APRS as the AX.25 destination") &&
           check(AprsMessage::Codec::decode(transport.payloads.first(), &decoded, &error) &&
                 !decoded.isAck && decoded.destination == QStringLiteral("DL1BBB-9") &&
                 decoded.body == QStringLiteral("hello APRS") && decoded.messageId == messageId,
                 "on-air information must be an APRS message, not an AXCP packet");
}

bool testIncomingMessageIsAddressedAckedAndDeduplicated()
{
    FakeTransport transport;
    AprsChat::AprsMessageEngine::Config config;
    config.localCallsign = QStringLiteral("DL1AAA");
    AprsChat::AprsMessageEngine engine(&transport, config, [] { return QStringLiteral("1"); });
    int received = 0;
    QString receivedText;
    QObject::connect(&engine, &AprsChat::AprsMessageEngine::incomingMessage, &engine,
        [&received, &receivedText](const QString &source, const QString &id, const QString &text) {
            if (source == QStringLiteral("N0CALL") && id == QStringLiteral("B7")) {
                ++received;
                receivedText = text;
            }
        });

    QByteArray wire;
    QString error;
    if (!AprsMessage::Codec::encode(message(QStringLiteral("DL1AAA"),
            QStringLiteral("hello"), QStringLiteral("B7")), &wire, &error))
        return check(false, "synthetic APRS message did not encode");
    transport.receive(QStringLiteral("N0CALL"), wire);
    transport.receive(QStringLiteral("N0CALL"), wire);

    AprsMessage::Message ack;
    return check(received == 1 && receivedText == QStringLiteral("hello"),
                 "a repeated APRS message must be delivered only once") &&
           check(transport.destinations == QStringList{QStringLiteral("APRS"), QStringLiteral("APRS")},
                 "each received message copy must be acknowledged over APRS") &&
           check(AprsMessage::Codec::decode(transport.payloads.first(), &ack, &error) &&
                 ack.isAck && ack.destination == QStringLiteral("N0CALL") &&
                 ack.messageId == QStringLiteral("B7"),
                 "ACK must use the APRS ack text and address the original sender");
}

bool testAckMatchesPeerAndRetryExhaustion()
{
    FakeTransport transport;
    AprsChat::AprsMessageEngine::Config config;
    config.localCallsign = QStringLiteral("N0CALL");
    config.retryTimeoutMs = 100;
    config.maxRetries = 1;
    AprsChat::AprsMessageEngine engine(&transport, config, [] { return QStringLiteral("42"); });
    int success = 0;
    int failure = 0;
    QObject::connect(&engine, &AprsChat::AprsMessageEngine::messageStatusChanged, &engine,
        [&success, &failure](const QString &, bool delivered, const QString &) {
            delivered ? ++success : ++failure;
        });

    QString error;
    if (!engine.sendMessage(QStringLiteral("DL1AAA"), QStringLiteral("hello"), 0, nullptr, &error))
        return check(false, "outgoing APRS message did not send");
    QByteArray wrongAck;
    QByteArray correctAck;
    if (!AprsMessage::Codec::encode(AprsMessage::Message{true, QStringLiteral("N0CALL"), {},
            QStringLiteral("42")}, &wrongAck, &error))
        return check(false, "ACK test payload did not encode");
    transport.receive(QStringLiteral("N0CALL"), wrongAck);
    engine.tick(100);
    if (!check(transport.payloads.size() == 2 && success == 0,
               "ACK from the wrong peer must not complete a message; retry should occur"))
        return false;

    if (!AprsMessage::Codec::encode(AprsMessage::Message{true, QStringLiteral("N0CALL"), {},
            QStringLiteral("42")}, &correctAck, &error))
        return check(false, "ACK test payload did not encode");
    transport.receive(QStringLiteral("DL1AAA"), correctAck);
    engine.tick(200);
    return check(success == 1 && failure == 0 && transport.payloads.size() == 2,
                 "matching ACK must complete the pending message and cancel retries");
}

bool testDefaultRetryScheduleMatchesAprsmap()
{
    FakeTransport transport;
    AprsChat::AprsMessageEngine::Config config;
    config.localCallsign = QStringLiteral("N0CALL");
    if (!check(config.retryTimeoutMs == 120000 && config.maxRetries == 11,
               "default APRS retry schedule must match APRSMap's 120-second timer and 11 repeats"))
        return false;

    AprsChat::AprsMessageEngine engine(&transport, config, [] { return QStringLiteral("42"); });
    int failures = 0;
    QObject::connect(&engine, &AprsChat::AprsMessageEngine::messageStatusChanged, &engine,
        [&failures](const QString &, bool delivered, const QString &) {
            if (!delivered)
                ++failures;
        });

    QString error;
    if (!engine.sendMessage(QStringLiteral("DL1AAA"), QStringLiteral("hello"), 0, nullptr, &error))
        return check(false, "outgoing APRS message did not send");
    const QByteArray originalPayload = transport.payloads.first();
    if (!check(!originalPayload.endsWith('\r'),
               "APRS application payload must not append FlexPacket's transport CR"))
        return false;

    engine.tick(119999);
    if (!check(transport.payloads.size() == 1,
               "first APRS retransmission must wait for the full 120-second interval"))
        return false;

    for (int retry = 1; retry <= 11; ++retry) {
        engine.tick(static_cast<qint64>(retry) * 120000);
        if (!check(transport.payloads.size() == retry + 1 &&
                   transport.payloads.last() == originalPayload,
                   "each APRS retry must use the exact original message bytes"))
            return false;
    }
    engine.tick(1439999);
    if (!check(failures == 0 && transport.payloads.size() == 12,
               "message must remain pending through all 11 reference retransmissions"))
        return false;
    engine.tick(1440000);
    return check(failures == 1 && transport.payloads.size() == 12,
                 "message must time out only after the retry sequence is exhausted");
}
}

bool testDestroyingEngineUnregistersTransportCallback()
{
    FakeTransport transport;
    {
        AprsChat::AprsMessageEngine::Config config;
        config.localCallsign = QStringLiteral("N0CALL");
        AprsChat::AprsMessageEngine engine(&transport, config);
        if (!check(static_cast<bool>(transport.receiveHandler),
                   "engine must register its transport receive callback"))
            return false;
    }
    return check(!transport.receiveHandler,
                 "destroyed engine must unregister its transport receive callback");
}

bool testGroupOperationsUseStandardAprsMessages()
{
    FakeTransport transport;
    AprsChat::AprsMessageEngine::Config config;
    config.localCallsign = QStringLiteral("N0CALL");
    int nextId = 0;
    AprsChat::AprsMessageEngine engine(&transport, config,
        [&nextId] { return QString::number(++nextId); });
    QString error;
    QString joinId;
    QString leaveId;
    QString messageId;
    if (!engine.sendGroupJoinCommand(QStringLiteral("sota"), 10, &joinId, &error) ||
        !engine.sendGroupLeaveCommand(QStringLiteral("POTA"), 20, &leaveId, &error) ||
        !engine.sendGroupMessage(QStringLiteral("SOTA"), QStringLiteral("summit alert"),
                                 30, &messageId, &error)) {
        std::cerr << error.toStdString() << '\n';
        return false;
    }
    if (!check(transport.destinations == QStringList{
                   QStringLiteral("APRS"), QStringLiteral("APRS"), QStringLiteral("APRS")},
               "group operations must use the APRS AX.25 destination"))
        return false;
    const QStringList expectedBodies{QStringLiteral("J SOTA"), QStringLiteral("U POTA"),
                                     QStringLiteral("CQ SOTA summit alert")};
    for (int i = 0; i < expectedBodies.size(); ++i) {
        AprsMessage::Message decoded;
        if (!AprsMessage::Codec::decode(transport.payloads.at(i), &decoded, &error) ||
            !check(decoded.destination == QStringLiteral("ANSRVR") &&
                   decoded.body == expectedBodies.at(i),
                   "group command must be a standard APRS message addressed to ANSRVR"))
            return false;
    }
    return check(!joinId.isEmpty() && !leaveId.isEmpty() && !messageId.isEmpty(),
                 "each group operation must retain standard APRS message IDs");
}

bool testGroupMessageRejectsTextBeyondFullAprsLimit()
{
    FakeTransport transport;
    AprsChat::AprsMessageEngine::Config config;
    config.localCallsign = QStringLiteral("N0CALL");
    AprsChat::AprsMessageEngine engine(&transport, config, [] { return QStringLiteral("A1"); });
    QString error;
    const bool sent = engine.sendGroupMessage(QStringLiteral("SOTA"), QString(60, QLatin1Char('x')),
                                              10, nullptr, &error);
    return check(!sent && transport.payloads.isEmpty(),
                 "group prefix must count toward the APRS 67-character text limit");
}

int main()
{
    return testSendsStandardAprsMessageToAprsDestination() &&
           testIncomingMessageIsAddressedAckedAndDeduplicated() &&
           testAckMatchesPeerAndRetryExhaustion() &&
           testDefaultRetryScheduleMatchesAprsmap() &&
           testDestroyingEngineUnregistersTransportCallback() &&
           testGroupOperationsUseStandardAprsMessages() &&
           testGroupMessageRejectsTextBeyondFullAprsLimit() ? 0 : 1;
}
