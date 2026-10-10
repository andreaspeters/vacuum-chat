#include "axcpcodec.h"

#include <limits>

namespace
{
const quint8 ReservedFlags = 0xe0;

bool setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

bool isKnownType(quint8 type)
{
    return type >= static_cast<quint8>(Axcp::Type::Hello) &&
        type <= static_cast<quint8>(Axcp::Type::Info);
}

quint8 byteAt(const QByteArray &data, int index)
{
    return static_cast<quint8>(static_cast<unsigned char>(data.at(index)));
}

bool isContinuationByte(quint8 byte)
{
    return byte >= 0x80 && byte <= 0xbf;
}

bool isValidUtf8(const QByteArray &text)
{
    int index = 0;
    while (index < text.size())
    {
        const quint8 first = byteAt(text, index);
        if (first <= 0x7f)
        {
            ++index;
            continue;
        }

        if (first >= 0xc2 && first <= 0xdf)
        {
            if (index + 1 >= text.size() || !isContinuationByte(byteAt(text, index + 1)))
                return false;
            index += 2;
            continue;
        }

        if (first >= 0xe0 && first <= 0xef)
        {
            if (index + 2 >= text.size())
                return false;
            const quint8 second = byteAt(text, index + 1);
            const quint8 third = byteAt(text, index + 2);
            if (!isContinuationByte(third) ||
                (first == 0xe0 ? (second < 0xa0 || second > 0xbf) :
                 first == 0xed ? (second < 0x80 || second > 0x9f) :
                 !isContinuationByte(second)))
                return false;
            index += 3;
            continue;
        }

        if (first >= 0xf0 && first <= 0xf4)
        {
            if (index + 3 >= text.size())
                return false;
            const quint8 second = byteAt(text, index + 1);
            if (!isContinuationByte(byteAt(text, index + 2)) ||
                !isContinuationByte(byteAt(text, index + 3)) ||
                (first == 0xf0 ? (second < 0x90 || second > 0xbf) :
                 first == 0xf4 ? (second < 0x80 || second > 0x8f) :
                 !isContinuationByte(second)))
                return false;
            index += 4;
            continue;
        }

        return false;
    }
    return true;
}
}

namespace Axcp
{
bool Codec::encode(const Packet &packet, QByteArray *encoded, QString *error,
                   int maximumPayloadBytes)
{
    if (error)
        error->clear();
    if (!encoded)
        return setError(error, QStringLiteral("output buffer is null"));
    if (maximumPayloadBytes < 0)
        return setError(error, QStringLiteral("maximum payload size is negative"));

    const quint8 type = static_cast<quint8>(packet.type);
    if (!isKnownType(type))
        return setError(error, QStringLiteral("unknown packet type"));
    if (packet.flags & ReservedFlags)
        return setError(error, QStringLiteral("reserved flag bits must be zero"));

    const bool fragmented = packet.flags & FragmentedFlag;
    const bool finalFragment = packet.flags & FinalFragmentFlag;
    if (fragmented)
    {
        if (packet.type != Type::Message)
            return setError(error, QStringLiteral("only chat messages may be fragmented"));
        if (packet.fragmentCount < 2 || packet.fragmentIndex >= packet.fragmentCount)
            return setError(error, QStringLiteral("invalid fragment index or count"));
        if (finalFragment != (packet.fragmentIndex + 1 == packet.fragmentCount))
            return setError(error, QStringLiteral("final-fragment flag does not match fragment index"));
    }
    else
    {
        if (finalFragment)
            return setError(error, QStringLiteral("final-fragment flag requires fragmentation"));
        if (packet.fragmentIndex != 0 || packet.fragmentCount != 0)
            return setError(error, QStringLiteral("fragment metadata requires fragmentation"));
    }

    if (packet.payload.size() > maximumPayloadBytes)
        return setError(error, QStringLiteral("payload exceeds configured maximum"));
    if (packet.type == Type::Message && !isValidUtf8(packet.payload))
        return setError(error, QStringLiteral("message payload is not valid UTF-8"));
    if (packet.payload.size() > std::numeric_limits<int>::max() - HeaderSize - 2)
        return setError(error, QStringLiteral("payload is too large to encode"));

    QByteArray frame;
    frame.reserve(HeaderSize + (fragmented ? 2 : 0) + packet.payload.size());
    frame.append(static_cast<char>(Version));
    frame.append(static_cast<char>(type));
    frame.append(static_cast<char>(packet.flags));
    frame.append(static_cast<char>((packet.messageId >> 24) & 0xff));
    frame.append(static_cast<char>((packet.messageId >> 16) & 0xff));
    frame.append(static_cast<char>((packet.messageId >> 8) & 0xff));
    frame.append(static_cast<char>(packet.messageId & 0xff));
    frame.append(static_cast<char>((packet.sequence >> 8) & 0xff));
    frame.append(static_cast<char>(packet.sequence & 0xff));
    if (fragmented)
    {
        frame.append(static_cast<char>(packet.fragmentIndex));
        frame.append(static_cast<char>(packet.fragmentCount));
    }
    frame.append(packet.payload);
    *encoded = frame;
    return true;
}

bool Codec::decode(const QByteArray &encoded, Packet *packet, QString *error,
                   int maximumPayloadBytes)
{
    if (error)
        error->clear();
    if (!packet)
        return setError(error, QStringLiteral("output packet is null"));
    *packet = Packet();
    if (maximumPayloadBytes < 0)
        return setError(error, QStringLiteral("maximum payload size is negative"));
    if (encoded.size() < HeaderSize)
        return setError(error, QStringLiteral("packet is shorter than the fixed header"));
    if (byteAt(encoded, 0) != Version)
        return setError(error, QStringLiteral("unsupported protocol version"));

    const quint8 type = byteAt(encoded, 1);
    if (!isKnownType(type))
        return setError(error, QStringLiteral("unknown packet type"));
    const quint8 flags = byteAt(encoded, 2);
    if (flags & ReservedFlags)
        return setError(error, QStringLiteral("reserved flag bits must be zero"));

    const bool fragmented = flags & FragmentedFlag;
    const bool finalFragment = flags & FinalFragmentFlag;
    int payloadOffset = HeaderSize;
    quint8 fragmentIndex = 0;
    quint8 fragmentCount = 0;
    if (fragmented)
    {
        if (type != static_cast<quint8>(Type::Message))
            return setError(error, QStringLiteral("only chat messages may be fragmented"));
        if (encoded.size() < HeaderSize + 2)
            return setError(error, QStringLiteral("fragment header is incomplete"));
        fragmentIndex = byteAt(encoded, HeaderSize);
        fragmentCount = byteAt(encoded, HeaderSize + 1);
        payloadOffset += 2;
        if (fragmentCount < 2 || fragmentIndex >= fragmentCount)
            return setError(error, QStringLiteral("invalid fragment index or count"));
        if (finalFragment != (fragmentIndex + 1 == fragmentCount))
            return setError(error, QStringLiteral("final-fragment flag does not match fragment index"));
    }
    else if (finalFragment)
    {
        return setError(error, QStringLiteral("final-fragment flag requires fragmentation"));
    }

    const int payloadSize = encoded.size() - payloadOffset;
    if (payloadSize > maximumPayloadBytes)
        return setError(error, QStringLiteral("payload exceeds configured maximum"));
    const QByteArray payload = encoded.mid(payloadOffset, payloadSize);
    if (type == static_cast<quint8>(Type::Message) && !isValidUtf8(payload))
        return setError(error, QStringLiteral("message payload is not valid UTF-8"));

    packet->type = static_cast<Type>(type);
    packet->flags = flags;
    packet->messageId = (static_cast<quint32>(byteAt(encoded, 3)) << 24) |
        (static_cast<quint32>(byteAt(encoded, 4)) << 16) |
        (static_cast<quint32>(byteAt(encoded, 5)) << 8) |
        static_cast<quint32>(byteAt(encoded, 6));
    packet->sequence = (static_cast<quint16>(byteAt(encoded, 7)) << 8) |
        static_cast<quint16>(byteAt(encoded, 8));
    packet->fragmentIndex = fragmentIndex;
    packet->fragmentCount = fragmentCount;
    packet->payload = payload;
    return true;
}
}
