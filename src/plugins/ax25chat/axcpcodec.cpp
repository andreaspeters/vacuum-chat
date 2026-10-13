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

bool isValidCallsign(const QByteArray &callsign)
{
    if (callsign.isEmpty() || callsign.size() > 9)
        return false;
    const int separator = callsign.indexOf('-');
    const int baseLength = separator < 0 ? callsign.size() : separator;
    if (baseLength < 1 || baseLength > 6)
        return false;
    for (int i = 0; i < baseLength; ++i) {
        const char c = callsign.at(i);
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
            return false;
    }
    if (separator < 0)
        return true;
    if (separator == callsign.size() - 1 || callsign.size() - separator > 3)
        return false;
    bool ok = false;
    const int ssid = callsign.mid(separator + 1).toInt(&ok);
    return ok && ssid >= 0 && ssid <= 15;
}

bool needsDestination(Axcp::Type type)
{
    return type == Axcp::Type::Message || type == Axcp::Type::Ack;
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

void appendEscapedByte(QByteArray *text, quint8 byte)
{
    static const char hex[] = "0123456789ABCDEF";
    text->append('%');
    text->append(hex[byte >> 4]);
    text->append(hex[byte & 0x0f]);
}

void appendHexByte(QByteArray *text, quint8 byte)
{
    static const char hex[] = "0123456789ABCDEF";
    text->append(hex[byte >> 4]);
    text->append(hex[byte & 0x0f]);
}

QByteArray encodeHexBytes(const QByteArray &bytes)
{
    QByteArray text;
    text.reserve(bytes.size() * 2);
    for (const char rawByte : bytes)
        appendHexByte(&text, static_cast<quint8>(static_cast<unsigned char>(rawByte)));
    return text;
}

QByteArray encodeTextPart(const QByteArray &bytes, bool preserveUtf8)
{
    QByteArray text;
    text.reserve(bytes.size());
    for (const char rawByte : bytes)
    {
        const quint8 byte = static_cast<quint8>(static_cast<unsigned char>(rawByte));
        const bool safeAscii = byte >= 0x20 && byte <= 0x7e && byte != '%';
        if (safeAscii || (preserveUtf8 && byte >= 0x80))
            text.append(rawByte);
        else
            appendEscapedByte(&text, byte);
    }
    return text;
}

int hexValue(char value)
{
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'A' && value <= 'F')
        return value - 'A' + 10;
    if (value >= 'a' && value <= 'f')
        return value - 'a' + 10;
    return -1;
}

bool decodeTextWire(const QByteArray &encoded, QByteArray *binary, QString *error)
{
    QByteArray text = encoded;
    if (text.endsWith('\r'))
        text.chop(1); // FlexPacket appends CR when sending Code-0 data.
    if (!text.startsWith("~3"))
        return setError(error, QStringLiteral("unsupported AXCP text-wire version"));
    if (!isValidUtf8(text))
        return setError(error, QStringLiteral("AXCP text wire is not valid UTF-8"));

    const int fixedHeaderBytes = Axcp::Codec::HeaderSize + 1;
    const int fixedHeaderTextSize = fixedHeaderBytes * 2;
    int payloadStart = 2 + fixedHeaderTextSize;
    if (text.size() <= payloadStart || text.at(payloadStart) != '|')
        return setError(error, QStringLiteral("AXCP text wire has no destination separator"));

    QByteArray decoded;
    decoded.reserve(text.size());
    for (int index = 0; index < fixedHeaderTextSize; index += 2)
    {
        const int high = hexValue(text.at(2 + index));
        const int low = hexValue(text.at(3 + index));
        if (high < 0 || low < 0)
            return setError(error, QStringLiteral("invalid hexadecimal AXCP header"));
        decoded.append(static_cast<char>((high << 4) | low));
    }

    const int destinationLength = byteAt(decoded, Axcp::Codec::HeaderSize);
    if (destinationLength > 9)
        return setError(error, QStringLiteral("destination length is invalid"));
    ++payloadStart;
    if (text.size() < payloadStart + destinationLength + 1)
        return setError(error, QStringLiteral("destination callsign is truncated"));
    decoded += text.mid(payloadStart, destinationLength);
    payloadStart += destinationLength;

    const bool fragmented = byteAt(decoded, 2) & Axcp::Codec::FragmentedFlag;
    if (fragmented)
    {
        if (text.size() < payloadStart + 5 || text.at(payloadStart) != '|')
            return setError(error, QStringLiteral("fragment metadata is missing"));
        ++payloadStart;
        for (int index = 0; index < 4; index += 2)
        {
            const int high = hexValue(text.at(payloadStart + index));
            const int low = hexValue(text.at(payloadStart + index + 1));
            if (high < 0 || low < 0)
                return setError(error, QStringLiteral("invalid hexadecimal fragment metadata"));
            decoded.append(static_cast<char>((high << 4) | low));
        }
        payloadStart += 4;
    }
    if (payloadStart >= text.size() || text.at(payloadStart) != ':')
        return setError(error, QStringLiteral("AXCP text wire has no payload separator"));
    ++payloadStart;

    for (int index = payloadStart; index < text.size(); ++index)
    {
        const quint8 byte = static_cast<quint8>(static_cast<unsigned char>(text.at(index)));
        if (byte == '%')
        {
            if (index + 2 >= text.size())
                return setError(error, QStringLiteral("truncated AXCP text escape"));
            const int high = hexValue(text.at(index + 1));
            const int low = hexValue(text.at(index + 2));
            if (high < 0 || low < 0)
                return setError(error, QStringLiteral("invalid AXCP text escape"));
            decoded.append(static_cast<char>((high << 4) | low));
            index += 2;
        }
        else
        {
            if (byte < 0x20 || byte == 0x7f)
                return setError(error, QStringLiteral("unescaped control byte in AXCP text wire"));
            decoded.append(static_cast<char>(byte));
        }
    }
    *binary = decoded;
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

    const QByteArray destination = packet.destination.toLatin1();
    if (QString::fromLatin1(destination) != packet.destination ||
        (needsDestination(packet.type) && destination.isEmpty()) ||
        (!destination.isEmpty() && !isValidCallsign(destination)))
        return setError(error, QStringLiteral("invalid or missing AXCP destination callsign"));

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
    const int fragmentHeaderSize = fragmented ? 2 : 0;
    const int binaryHeaderSize = HeaderSize + 1 + destination.size() + fragmentHeaderSize;
    const int maxEncodedPayloadSize =
        (std::numeric_limits<int>::max() - 3 * binaryHeaderSize - 2) / 3;
    if (packet.payload.size() > maxEncodedPayloadSize)
        return setError(error, QStringLiteral("payload is too large to encode"));

    QByteArray frame;
    frame.reserve(binaryHeaderSize + packet.payload.size());
    frame.append(static_cast<char>(Version));
    frame.append(static_cast<char>(type));
    frame.append(static_cast<char>(packet.flags));
    frame.append(static_cast<char>((packet.messageId >> 24) & 0xff));
    frame.append(static_cast<char>((packet.messageId >> 16) & 0xff));
    frame.append(static_cast<char>((packet.messageId >> 8) & 0xff));
    frame.append(static_cast<char>(packet.messageId & 0xff));
    frame.append(static_cast<char>((packet.sequence >> 8) & 0xff));
    frame.append(static_cast<char>(packet.sequence & 0xff));
    frame.append(static_cast<char>(destination.size()));
    frame.append(destination);
    if (fragmented)
    {
        frame.append(static_cast<char>(packet.fragmentIndex));
        frame.append(static_cast<char>(packet.fragmentCount));
    }
    frame.append(packet.payload);
    QByteArray textWire("~3");
    const int fixedHeaderBytes = HeaderSize + 1;
    textWire += encodeHexBytes(frame.left(fixedHeaderBytes));
    textWire += '|';
    textWire += destination;
    if (fragmented) {
        textWire += '|';
        textWire += encodeHexBytes(frame.mid(fixedHeaderBytes + destination.size(), 2));
    }
    textWire += ':';
    textWire += encodeTextPart(packet.payload, packet.type == Type::Message);
    *encoded = textWire;
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
    QByteArray frame;
    if (!decodeTextWire(encoded, &frame, error))
        return false;
    if (frame.size() < HeaderSize + 1)
        return setError(error, QStringLiteral("packet is shorter than the fixed header and destination length"));
    if (byteAt(frame, 0) != Version)
        return setError(error, QStringLiteral("unsupported protocol version"));

    const quint8 type = byteAt(frame, 1);
    if (!isKnownType(type))
        return setError(error, QStringLiteral("unknown packet type"));
    const quint8 flags = byteAt(frame, 2);
    if (flags & ReservedFlags)
        return setError(error, QStringLiteral("reserved flag bits must be zero"));

    const int destinationLength = byteAt(frame, HeaderSize);
    if (destinationLength > 9 || frame.size() < HeaderSize + 1 + destinationLength)
        return setError(error, QStringLiteral("destination field is invalid or incomplete"));
    const QByteArray destinationBytes = frame.mid(HeaderSize + 1, destinationLength);
    const QString destination = QString::fromLatin1(destinationBytes);
    if (needsDestination(static_cast<Type>(type)) && destination.isEmpty())
        return setError(error, QStringLiteral("unicast packet has no destination callsign"));
    if (!destination.isEmpty() && !isValidCallsign(destinationBytes))
        return setError(error, QStringLiteral("destination callsign is malformed"));

    const bool fragmented = flags & FragmentedFlag;
    const bool finalFragment = flags & FinalFragmentFlag;
    int payloadOffset = HeaderSize + 1 + destinationLength;
    quint8 fragmentIndex = 0;
    quint8 fragmentCount = 0;
    if (fragmented)
    {
        if (type != static_cast<quint8>(Type::Message))
            return setError(error, QStringLiteral("only chat messages may be fragmented"));
        if (frame.size() < payloadOffset + 2)
            return setError(error, QStringLiteral("fragment header is incomplete"));
        fragmentIndex = byteAt(frame, payloadOffset);
        fragmentCount = byteAt(frame, payloadOffset + 1);
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

    const int payloadSize = frame.size() - payloadOffset;
    if (payloadSize > maximumPayloadBytes)
        return setError(error, QStringLiteral("payload exceeds configured maximum"));
    const QByteArray payload = frame.mid(payloadOffset, payloadSize);
    if (type == static_cast<quint8>(Type::Message) && !isValidUtf8(payload))
        return setError(error, QStringLiteral("message payload is not valid UTF-8"));

    packet->type = static_cast<Type>(type);
    packet->flags = flags;
    packet->messageId = (static_cast<quint32>(byteAt(frame, 3)) << 24) |
        (static_cast<quint32>(byteAt(frame, 4)) << 16) |
        (static_cast<quint32>(byteAt(frame, 5)) << 8) |
        static_cast<quint32>(byteAt(frame, 6));
    packet->sequence = (static_cast<quint16>(byteAt(frame, 7)) << 8) |
        static_cast<quint16>(byteAt(frame, 8));
    packet->destination = destination;
    packet->fragmentIndex = fragmentIndex;
    packet->fragmentCount = fragmentCount;
    packet->payload = payload;
    return true;
}
}
