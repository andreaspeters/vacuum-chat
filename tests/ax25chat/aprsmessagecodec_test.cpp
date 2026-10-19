#include "aprsmessagecodec.h"

#include <iostream>

namespace
{
bool check(bool condition, const char *message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

bool testMessageRoundTrip()
{
    AprsMessage::Message input;
    input.destination = QStringLiteral("DL1BBB-9");
    input.body = QStringLiteral("hello APRS");
    input.messageId = QStringLiteral("A9z01");

    QByteArray wire;
    QString error;
    if (!AprsMessage::Codec::encode(input, &wire, &error)) {
        std::cerr << error.toStdString() << '\n';
        return false;
    }
    if (!check(wire == QByteArrayLiteral(":DL1BBB-9 :hello APRS{A9z01"),
               "message wire format must match APRS fixed-width addressee and ID"))
        return false;

    AprsMessage::Message output;
    if (!AprsMessage::Codec::decode(wire, &output, &error)) {
        std::cerr << error.toStdString() << '\n';
        return false;
    }
    return check(!output.isAck && output.destination == input.destination &&
                 output.body == input.body && output.messageId == input.messageId,
                 "message fields must round-trip");
}

bool testPaddedAddresseeAndNoId()
{
    const QByteArray wire = QByteArrayLiteral(":N0CALL   :no ack requested");
    AprsMessage::Message output;
    QString error;
    if (!AprsMessage::Codec::decode(wire, &output, &error)) {
        std::cerr << error.toStdString() << '\n';
        return false;
    }
    if (!check(output.destination == QStringLiteral("N0CALL") && output.messageId.isEmpty(),
               "decoder must trim addressee padding and accept messages without IDs"))
        return false;
    QByteArray encoded;
    return check(AprsMessage::Codec::encode(output, &encoded, &error) && encoded == wire,
                 "message without an ID must encode without an ACK marker");
}

bool testAckRoundTrip()
{
    AprsMessage::Message ack;
    ack.isAck = true;
    ack.destination = QStringLiteral("DL1BBB-9");
    ack.messageId = QStringLiteral("A9z01");

    QByteArray wire;
    QString error;
    if (!AprsMessage::Codec::encode(ack, &wire, &error) ||
        !check(wire == QByteArrayLiteral(":DL1BBB-9 :ackA9z01"),
               "ACK wire format must use ack followed by message ID"))
        return false;
    AprsMessage::Message decoded;
    return check(AprsMessage::Codec::decode(wire, &decoded, &error) && decoded.isAck &&
                 decoded.destination == ack.destination && decoded.messageId == ack.messageId,
                 "ACK fields must round-trip");
}

bool testRejectMalformedInput()
{
    AprsMessage::Message decoded;
    QString error;
    if (!check(!AprsMessage::Codec::decode(QByteArrayLiteral(": N0CALL  :text"), &decoded, &error),
               "leading padding in the addressee field must be rejected") ||
        !check(AprsMessage::Codec::decode(QByteArrayLiteral(":N0CALL   :acknowledge"), &decoded, &error) &&
               !decoded.isAck && decoded.body == QStringLiteral("acknowledge"),
               "ordinary text beginning with ack must not be misclassified as an ACK"))
        return false;
    if (!check(!AprsMessage::Codec::decode(QByteArrayLiteral(":N0CALL  :text"), &decoded, &error),
               "short addressee field must be rejected") ||
        !check(!AprsMessage::Codec::decode(QByteArrayLiteral(":N0CALL   :text{bad!"), &decoded, &error),
               "invalid message ID must be rejected") ||
        !check(!AprsMessage::Codec::decode(QByteArrayLiteral(":N0CALL   :text{123456"), &decoded, &error),
               "message ID longer than five characters must be rejected") ||
        !check(!AprsMessage::Codec::decode(QByteArrayLiteral(":N0CALL   :text\n"), &decoded, &error),
               "control characters must be rejected") ||
        !check(!AprsMessage::Codec::decode(QByteArrayLiteral(":N0CALL   :" ) + QByteArray(68, 'x'), &decoded, &error),
               "message bodies longer than 67 characters must be rejected"))
        return false;

    AprsMessage::Message invalid;
    invalid.destination = QStringLiteral("N0 CALL");
    invalid.body = QStringLiteral("message");
    QByteArray encoded;
    return check(!AprsMessage::Codec::encode(invalid, &encoded, &error),
                 "destination containing spaces must be rejected");
}
}

int main()
{
    return testMessageRoundTrip() && testPaddedAddresseeAndNoId() &&
           testAckRoundTrip() && testRejectMalformedInput() ? 0 : 1;
}
