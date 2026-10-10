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

bool testBigEndianRoundTrip()
{
    Axcp::Packet packet;
    packet.type = Axcp::Type::Message;
    packet.flags = Axcp::Codec::AckRequiredFlag;
    packet.messageId = 0xa31f92c1;
    packet.sequence = 0x1234;
    packet.payload = QByteArray::fromHex("68c3a9"); // UTF-8 "hé"

    QByteArray encoded;
    QString error;
    if (!Axcp::Codec::encode(packet, &encoded, &error))
        return fail("valid AXCP packet did not encode", error);
    if (encoded != QByteArray::fromHex("010208a31f92c1123468c3a9"))
        return fail("AXCP fields were not encoded in the specified big-endian layout");

    Axcp::Packet decoded;
    if (!Axcp::Codec::decode(encoded, &decoded, &error))
        return fail("encoded AXCP packet did not decode", error);
    if (decoded.type != packet.type || decoded.flags != packet.flags ||
        decoded.messageId != packet.messageId || decoded.sequence != packet.sequence ||
        decoded.payload != packet.payload || decoded.fragmentCount != 0)
        return fail("AXCP packet round trip changed field values");
    return true;
}

bool testFragmentedPackets()
{
    Axcp::Packet first;
    first.type = Axcp::Type::Message;
    first.flags = Axcp::Codec::FragmentedFlag | Axcp::Codec::AckRequiredFlag;
    first.messageId = 0x10203040;
    first.sequence = 7;
    first.fragmentIndex = 0;
    first.fragmentCount = 2;
    first.payload = QByteArrayLiteral("hello ");

    QByteArray encoded;
    QString error;
    if (!Axcp::Codec::encode(first, &encoded, &error))
        return fail("valid first fragment did not encode", error);
    if (encoded.left(Axcp::Codec::HeaderSize).toHex() != QByteArrayLiteral("010209102030400007"))
        return fail("fragment packet header does not match the AXCP wire layout");
    if (encoded.mid(Axcp::Codec::HeaderSize, 2) != QByteArray::fromHex("0002"))
        return fail("fragment index/count were not encoded after the fixed header");

    Axcp::Packet decoded;
    if (!Axcp::Codec::decode(encoded, &decoded, &error) ||
        decoded.fragmentIndex != 0 || decoded.fragmentCount != 2 || decoded.payload != first.payload)
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
    if (Axcp::Codec::decode(QByteArray(8, '\0'), &decoded, &error) || decoded.messageId != 0)
        return fail("short header was accepted or failure did not reset the result");

    if (Axcp::Codec::decode(QByteArray::fromHex("020200000000000000"), &decoded, &error) ||
        Axcp::Codec::decode(QByteArray::fromHex("010b00000000000000"), &decoded, &error) ||
        Axcp::Codec::decode(QByteArray::fromHex("0102e0000000000000"), &decoded, &error))
        return fail("unknown version/type or reserved flag bits were accepted");

    Axcp::Packet packet;
    packet.payload = QByteArray(181, 'x');
    QByteArray encoded;
    if (Axcp::Codec::encode(packet, &encoded, &error))
        return fail("payload exceeding the configured AXCP limit was accepted");

    packet.payload = QByteArray::fromHex("c328"); // Invalid UTF-8 continuation byte.
    if (Axcp::Codec::encode(packet, &encoded, &error))
        return fail("invalid UTF-8 message payload was accepted");
    const QByteArray invalidUtf8Packet =
        QByteArray::fromHex("010200000000000000") + QByteArray::fromHex("c328");
    if (Axcp::Codec::decode(invalidUtf8Packet, &decoded, &error))
        return fail("decoder accepted an invalid UTF-8 message payload");

    const QByteArray oversizedPacket =
        QByteArray::fromHex("010200000000000000") + QByteArray(181, 'x');
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
    configurableLimitPacket.payload = QByteArray(181, 'x');
    if (!Axcp::Codec::encode(configurableLimitPacket, &encoded, &error, 181))
        return fail("caller-configured payload limit was ignored", error);
    if (!Axcp::Codec::decode(encoded, &decoded, &error, 181))
        return fail("decoder ignored the caller-configured payload limit", error);
    return true;
}
}

int main()
{
    if (!testBigEndianRoundTrip() || !testFragmentedPackets() || !testRejectMalformedPackets())
        return 1;
    return 0;
}
