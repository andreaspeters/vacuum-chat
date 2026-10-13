#include "axcpcodec.h"

#include <iostream>

namespace
{
bool fail(const char *message, const QString &detail = QString())
{
    std::cerr << message;
    if (!detail.isEmpty())
        std::cerr << ": " << detail.toStdString();
    std::cerr << '\n';
    return false;
}

bool testUtf8WireRoundTrip()
{
    Axcp::Packet packet;
    packet.type = Axcp::Type::Message;
    packet.flags = Axcp::Codec::AckRequiredFlag;
    packet.messageId = 0xa31f92c1;
    packet.sequence = 0x1234;
    packet.destination = QStringLiteral("DL1BBB-9");
    packet.payload = QString::fromUtf8("hé |: %20 \r\n").toUtf8();

    QByteArray encoded;
    QString error;
    if (!Axcp::Codec::encode(packet, &encoded, &error))
        return fail("valid AXCP packet did not encode", error);
    const QByteArray expectedWire = QString::fromUtf8(
        "~3020208A31F92C1123408|DL1BBB-9:hé |: %2520 %0D%0A").toUtf8();
    if (encoded != expectedWire || encoded.contains('\r') || encoded.contains('\n') ||
        QString::fromUtf8(encoded).toUtf8() != encoded)
        return fail("AXCP packet is not compact, printable/UTF-8-safe wire text");

    Axcp::Packet decoded;
    if (!Axcp::Codec::decode(encoded + '\r', &decoded, &error))
        return fail("FlexPacket-terminated AXCP packet did not decode", error);
    if (decoded.type != packet.type || decoded.flags != packet.flags ||
        decoded.messageId != packet.messageId || decoded.sequence != packet.sequence ||
        decoded.destination != packet.destination || decoded.payload != packet.payload ||
        decoded.fragmentCount != 0)
        return fail("AXCP text-wire round trip changed field values");
    return true;
}

bool testFragmentedPackets()
{
    Axcp::Packet first;
    first.type = Axcp::Type::Message;
    first.flags = Axcp::Codec::FragmentedFlag | Axcp::Codec::AckRequiredFlag;
    first.messageId = 0x10203040;
    first.sequence = 7;
    first.destination = QStringLiteral("DL1BBB-9");
    first.fragmentIndex = 0;
    first.fragmentCount = 2;
    first.payload = QByteArrayLiteral("hello ");

    QByteArray encoded;
    QString error;
    if (!Axcp::Codec::encode(first, &encoded, &error))
        return fail("valid first fragment did not encode", error);
    if (!encoded.startsWith(QByteArrayLiteral("~302020910203040000708|DL1BBB-9|0002:")) ||
        encoded.contains('\r') || encoded.contains('\n'))
        return fail("fragment packet is not valid single-line AXCP text");

    Axcp::Packet decoded;
    if (!Axcp::Codec::decode(encoded, &decoded, &error) ||
        decoded.fragmentIndex != 0 || decoded.fragmentCount != 2 ||
        decoded.destination != first.destination || decoded.payload != first.payload)
        return fail("fragment metadata did not round trip", error);

    first.fragmentIndex = 1;
    first.flags |= Axcp::Codec::FinalFragmentFlag;
    first.payload = QByteArrayLiteral("world");
    if (!Axcp::Codec::encode(first, &encoded, &error) ||
        !Axcp::Codec::decode(encoded, &decoded, &error) ||
        !(decoded.flags & Axcp::Codec::FinalFragmentFlag))
        return fail("last fragment must carry the final-fragment flag", error);
    return true;
}

bool testRejectMalformedPackets()
{
    Axcp::Packet decoded;
    decoded.messageId = 77;
    QString error;
    if (Axcp::Codec::decode(QByteArrayLiteral("~3"), &decoded, &error) || decoded.messageId != 0)
        return fail("short header was accepted or failure did not reset the result");

    const QByteArray validHeader = QByteArrayLiteral(
        "~302020000000000000006|N0CALL:");
    if (Axcp::Codec::decode(QByteArray::fromHex("020208a31f92c1123408444c314242422d3968c3a9"),
                            &decoded, &error) ||
        Axcp::Codec::decode(QByteArrayLiteral("~2"),
                            &decoded, &error) ||
        Axcp::Codec::decode(QByteArrayLiteral("~3020B0000000000000006|N0CALL:x"),
                            &decoded, &error) ||
        Axcp::Codec::decode(QByteArrayLiteral("~30202E000000000000006|N0CALL:x"),
                            &decoded, &error) ||
        Axcp::Codec::decode(QByteArrayLiteral("~302020000000000000000|:x"),
                            &decoded, &error))
        return fail("unknown version/type or reserved flag bits were accepted");

    Axcp::Packet packet;
    packet.destination = QStringLiteral("N0CALL");
    packet.payload = QByteArray(181, 'x');
    QByteArray encoded;
    if (Axcp::Codec::encode(packet, &encoded, &error))
        return fail("payload exceeding the configured AXCP limit was accepted");

    packet.payload = QByteArray::fromHex("c328"); // Invalid UTF-8 continuation byte.
    if (Axcp::Codec::encode(packet, &encoded, &error))
        return fail("invalid UTF-8 message payload was accepted");
    const QByteArray invalidUtf8Packet = validHeader + QByteArrayLiteral("%C3(");
    if (Axcp::Codec::decode(invalidUtf8Packet, &decoded, &error))
        return fail("decoder accepted an invalid UTF-8 message payload");
    if (Axcp::Codec::decode(validHeader + "x\n", &decoded, &error))
        return fail("decoder accepted an unescaped line feed in the wire payload");

    const QByteArray oversizedPacket = validHeader + QByteArray(181, 'x');
    if (Axcp::Codec::decode(oversizedPacket, &decoded, &error))
        return fail("decoder accepted a payload exceeding the configured maximum");

    packet.payload = QByteArrayLiteral("x");
    packet.flags = Axcp::Codec::FragmentedFlag;
    packet.fragmentIndex = 2;
    packet.fragmentCount = 2;
    if (Axcp::Codec::encode(packet, &encoded, &error))
        return fail("fragment index outside its count was accepted");

    packet.fragmentIndex = 0;
    packet.fragmentCount = 0;
    if (Axcp::Codec::encode(packet, &encoded, &error))
        return fail("fragment packet with an empty fragment count was accepted");

    packet.flags = Axcp::Codec::FinalFragmentFlag;
    packet.fragmentIndex = 0;
    packet.fragmentCount = 0;
    if (Axcp::Codec::encode(packet, &encoded, &error))
        return fail("final-fragment flag without fragmentation was accepted");

    Axcp::Packet configurableLimitPacket;
    configurableLimitPacket.destination = QStringLiteral("N0CALL");
    configurableLimitPacket.payload = QByteArray(181, 'x');
    if (!Axcp::Codec::encode(configurableLimitPacket, &encoded, &error, 181))
        return fail("caller-configured payload limit was ignored", error);
    if (!Axcp::Codec::decode(encoded, &decoded, &error, 181))
        return fail("decoder ignored the caller-configured payload limit", error);
    return true;
}

bool testEscapedBinaryPayload()
{
    Axcp::Packet packet;
    packet.type = Axcp::Type::Ack;
    packet.destination = QStringLiteral("DL1AAA");
    packet.payload = QByteArray::fromHex("000d25ff");

    QByteArray encoded;
    QString error;
    if (!Axcp::Codec::encode(packet, &encoded, &error) ||
        !encoded.endsWith(QByteArrayLiteral("%00%0D%25%FF")))
        return fail("binary ACK payload was not escaped into UTF-8-safe text", error);

    Axcp::Packet decoded;
    if (!Axcp::Codec::decode(encoded, &decoded, &error) || decoded.payload != packet.payload)
        return fail("escaped binary ACK payload did not round trip", error);
    return true;
}
}

int main()
{
    if (!testUtf8WireRoundTrip() || !testFragmentedPackets() ||
        !testRejectMalformedPackets() || !testEscapedBinaryPayload())
        return 1;
    return 0;
}
