#include "meshcorecodec.h"

#include <algorithm>
#include <limits>

namespace
{
int hexNibble(char value)
{
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'a' && value <= 'f')
        return value - 'a' + 10;
    if (value >= 'A' && value <= 'F')
        return value - 'A' + 10;
    return -1;
}

bool decodeHexBytes(const std::string &hex, std::vector<std::uint8_t> &bytes)
{
    if (hex.size() % 2 != 0)
        return false;

    bytes.clear();
    bytes.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        const int high = hexNibble(hex[i]);
        const int low = hexNibble(hex[i + 1]);
        if (high < 0 || low < 0)
            return false;
        bytes.push_back(static_cast<std::uint8_t>((high << 4) | low));
    }
    return true;
}

void appendLittleEndian32(std::vector<std::uint8_t> &payload, std::uint32_t value)
{
    payload.push_back(static_cast<std::uint8_t>(value & 0xffu));
    payload.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffu));
    payload.push_back(static_cast<std::uint8_t>((value >> 16) & 0xffu));
    payload.push_back(static_cast<std::uint8_t>((value >> 24) & 0xffu));
}
}

std::vector<std::uint8_t> MeshCoreCompanionCodec::encodeFrame(
    const std::vector<std::uint8_t> &payload)
{
    if (payload.size() > std::numeric_limits<std::uint16_t>::max())
        return {};

    const std::uint16_t length = static_cast<std::uint16_t>(payload.size());
    std::vector<std::uint8_t> frame;
    frame.reserve(payload.size() + 3);
    frame.push_back(static_cast<std::uint8_t>('<'));
    frame.push_back(static_cast<std::uint8_t>(length & 0xffu));
    frame.push_back(static_cast<std::uint8_t>((length >> 8) & 0xffu));
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

std::vector<std::uint8_t> MeshCoreCompanionCodec::encodeBLEPayload(
    const std::vector<std::uint8_t> &payload)
{
    return payload;
}

std::vector<std::uint8_t> MeshCoreCompanionCodec::decodeBLEPayload(
    const std::vector<std::uint8_t> &payload)
{
    return payload;
}

std::vector<std::uint8_t> MeshCoreCompanionCodec::encodeDirectTextCommand(
    std::uint8_t attempt, std::uint32_t timestamp,
    const std::vector<std::uint8_t> &destination,
    const std::string &text)
{
    // Direct text command payload:
    // 0x02, 0x00, attempt, timestamp(4 bytes LE), destination(6 bytes), UTF-8 text
    if (destination.size() != 6)
        return {};

    std::vector<std::uint8_t> payload;
    payload.reserve(1 + 1 + 1 + 4 + 6 + text.size());
    
    payload.push_back(0x02);           // command identifier
    payload.push_back(0x00);           // reserved/zero byte
    
    payload.push_back(attempt);        // attempt number
    
    // Add timestamp as little-endian uint32_t
    payload.push_back(static_cast<std::uint8_t>(timestamp & 0xffu));
    payload.push_back(static_cast<std::uint8_t>((timestamp >> 8) & 0xffu));
    payload.push_back(static_cast<std::uint8_t>((timestamp >> 16) & 0xffu));
    payload.push_back(static_cast<std::uint8_t>((timestamp >> 24) & 0xffu));
    
    // Add destination (6 bytes)
    payload.insert(payload.end(), destination.begin(), destination.end());
    
    // Add text as UTF-8
    payload.insert(payload.end(), text.begin(), text.end());
    
    return payload;
}

std::vector<std::uint8_t> MeshCoreCompanionCodec::encodeChannelTextCommand(
    std::uint8_t channelIndex, std::uint32_t timestamp,
    const std::string &text)
{
    // Channel text command payload:
    // 0x03, 0x00, channel index, timestamp(4 bytes LE), UTF-8 text
    std::vector<std::uint8_t> payload;
    payload.reserve(1 + 1 + 1 + 4 + text.size());
    
    payload.push_back(0x03);           // command identifier  
    payload.push_back(0x00);           // reserved/zero byte
    
    payload.push_back(channelIndex);   // channel index
    
    // Add timestamp as little-endian uint32_t
    payload.push_back(static_cast<std::uint8_t>(timestamp & 0xffu));
    payload.push_back(static_cast<std::uint8_t>((timestamp >> 8) & 0xffu));
    payload.push_back(static_cast<std::uint8_t>((timestamp >> 16) & 0xffu));
    payload.push_back(static_cast<std::uint8_t>((timestamp >> 24) & 0xffu));
    
    // Add text as UTF-8
    payload.insert(payload.end(), text.begin(), text.end());
    
    return payload;
}

std::vector<std::uint8_t> MeshCoreCompanionCodec::encodeSetChannel(
    std::uint8_t channelIndex,
    const std::string &name,
    const std::vector<std::uint8_t> &secret)
{
    if (channelIndex > 7 || name.size() > 32 ||
        name.find('\0') != std::string::npos || secret.size() != 16)
        return {};

    std::vector<std::uint8_t> payload;
    payload.reserve(50);
    payload.push_back(0x20);
    payload.push_back(channelIndex);
    payload.insert(payload.end(), name.begin(), name.end());
    payload.resize(34, 0);
    payload.insert(payload.end(), secret.begin(), secret.end());
    return payload;
}

std::vector<std::uint8_t> MeshCoreCompanionCodec::encodeAddUpdateContact(
    const std::string &publicKeyHex,
    std::uint8_t type,
    std::uint8_t flags,
    int outPathHashMode,
    int outPathLength,
    const std::string &outPathHex,
    const std::string &name,
    std::uint32_t lastAdvert,
    std::int32_t latitudeMicrodegrees,
    std::int32_t longitudeMicrodegrees)
{
    if (publicKeyHex.size() != 64 || name.size() > 31 ||
        name.find('\0') != std::string::npos)
        return {};

    std::vector<std::uint8_t> publicKeyBytes;
    if (!decodeHexBytes(publicKeyHex, publicKeyBytes) || publicKeyBytes.size() != 32)
        return {};

    const bool floodPath = outPathHashMode == -1 && outPathLength == -1;
    std::vector<std::uint8_t> outPathBytes;
    std::uint8_t encodedPathLength = 0xff;
    if (!floodPath) {
        if (outPathHashMode < 0 || outPathHashMode > 3 ||
            outPathLength < 0 || outPathLength > 63)
            return {};

        const std::size_t pathByteCount = static_cast<std::size_t>(outPathLength) *
            static_cast<std::size_t>(outPathHashMode + 1);
        if (pathByteCount > 64 || outPathHex.size() != pathByteCount * 2 ||
            !decodeHexBytes(outPathHex, outPathBytes) || outPathBytes.size() != pathByteCount)
            return {};

        encodedPathLength = static_cast<std::uint8_t>(
            (outPathHashMode << 6) | outPathLength);
    } else if (!outPathHex.empty()) {
        return {};
    }

    std::vector<std::uint8_t> payload;
    payload.reserve(144);
    payload.push_back(0x09);
    payload.insert(payload.end(), publicKeyBytes.begin(), publicKeyBytes.end());
    payload.push_back(type);
    payload.push_back(flags);
    payload.push_back(encodedPathLength);
    payload.insert(payload.end(), outPathBytes.begin(), outPathBytes.end());
    payload.resize(100, 0);
    payload.insert(payload.end(), name.begin(), name.end());
    payload.resize(132, 0);
    appendLittleEndian32(payload, lastAdvert);
    appendLittleEndian32(payload, static_cast<std::uint32_t>(latitudeMicrodegrees));
    appendLittleEndian32(payload, static_cast<std::uint32_t>(longitudeMicrodegrees));
    return payload;
}

bool MeshCoreCompanionCodec::decodeIncomingText(
    const std::vector<std::uint8_t> &packet, MeshCoreIncomingText &result)
{
    result = MeshCoreIncomingText();
    if (packet.empty())
        return false;

    bool isChannel = false;
    bool trimChannelNulPadding = false;
    std::size_t prefixStart = 0;
    std::size_t pathLengthIndex = 0;
    std::size_t textTypeIndex = 0;
    std::size_t timestampStart = 0;
    std::size_t textStart = 0;
    std::size_t channelIndex = 0;

    switch (packet[0]) {
    case 7: // CONTACT_MSG_RECV
        prefixStart = 1;
        pathLengthIndex = 7;
        textTypeIndex = 8;
        timestampStart = 9;
        textStart = 13;
        break;
    case 8: // CHANNEL_MSG_RECV
        isChannel = true;
        trimChannelNulPadding = true;
        channelIndex = 1;
        pathLengthIndex = 2;
        textTypeIndex = 3;
        timestampStart = 4;
        textStart = 8;
        break;
    case 16: // CONTACT_MSG_RECV_V3
        prefixStart = 4;
        pathLengthIndex = 10;
        textTypeIndex = 11;
        timestampStart = 12;
        textStart = 16;
        break;
    case 17: // CHANNEL_MSG_RECV_V3
        isChannel = true;
        channelIndex = 4;
        pathLengthIndex = 5;
        textTypeIndex = 6;
        timestampStart = 7;
        textStart = 11;
        break;
    default:
        return false;
    }

    if (packet.size() < textStart)
        return false;

    MeshCoreIncomingText decoded;
    const std::uint8_t textType = packet[textTypeIndex];
    const bool hasSignature = !isChannel && textType == 2;
    if (hasSignature && packet.size() - textStart < 4)
        return false;

    if (hasSignature) {
        static const char hex[] = "0123456789abcdef";
        decoded.signatureHex.reserve(8);
        for (std::size_t i = textStart; i < textStart + 4; ++i) {
            decoded.signatureHex.push_back(hex[packet[i] >> 4]);
            decoded.signatureHex.push_back(hex[packet[i] & 0x0f]);
        }
        textStart += 4;
    }

    decoded.isChannel = isChannel;
    decoded.pathLength = packet[pathLengthIndex];
    if (decoded.pathLength == 0xff) {
        decoded.pathHashMode = -1;
        decoded.pathLengthHops = 0xff;
    } else {
        decoded.pathHashMode = decoded.pathLength >> 6;
        decoded.pathLengthHops = decoded.pathLength & 0x3f;
    }
    decoded.textType = textType;
    decoded.hasSignature = hasSignature;
    if (packet[0] == 16 || packet[0] == 17) {
        int snr = packet[1];
        if (snr > 127)
            snr -= 256;
        decoded.hasSnr = true;
        decoded.snrQuarterDb = static_cast<std::int8_t>(snr);
    }
    if (isChannel)
        decoded.channelIndex = packet[channelIndex];
    else {
        static const char hex[] = "0123456789abcdef";
        decoded.senderPrefixHex.reserve(12);
        for (std::size_t i = 0; i < 6; ++i) {
            const std::uint8_t byte = packet[prefixStart + i];
            decoded.senderPrefixHex.push_back(hex[byte >> 4]);
            decoded.senderPrefixHex.push_back(hex[byte & 0x0f]);
        }
    }

    for (std::size_t i = 0; i < 4; ++i)
        decoded.senderTimestamp |=
            static_cast<std::uint32_t>(packet[timestampStart + i]) << (8 * i);

    decoded.text.reserve(packet.size() - textStart);
    for (std::size_t i = textStart; i < packet.size(); ++i)
        decoded.text.push_back(static_cast<char>(packet[i]));

    if (trimChannelNulPadding) {
        while (!decoded.text.empty() && decoded.text.front() == '\0')
            decoded.text.erase(decoded.text.begin());
        while (!decoded.text.empty() && decoded.text.back() == '\0')
            decoded.text.pop_back();
    }

    result = decoded;
    return true;
}

bool MeshCoreCompanionCodec::decodeContactRecord(
    const std::vector<std::uint8_t> &packet, MeshCoreContactRecord &result)
{
    result = MeshCoreContactRecord();
    const std::size_t contactSize = 148;
    if (packet.size() < contactSize || (packet[0] != 3 && packet[0] != 0x8a))
        return false;

    MeshCoreContactRecord decoded;
    static const char hex[] = "0123456789abcdef";
    decoded.publicKeyHex.reserve(64);
    for (std::size_t i = 1; i < 33; ++i) {
        decoded.publicKeyHex.push_back(hex[packet[i] >> 4]);
        decoded.publicKeyHex.push_back(hex[packet[i] & 0x0f]);
    }

    decoded.type = packet[33];
    decoded.flags = packet[34];
    const std::uint8_t encodedPathLength = packet[35];
    if (encodedPathLength == 0xff) {
        decoded.outPathHashMode = -1;
        decoded.outPathLength = -1;
    } else {
        decoded.outPathHashMode = encodedPathLength >> 6;
        decoded.outPathLength = encodedPathLength & 0x3f;
        const std::size_t pathByteCount =
            static_cast<std::size_t>(decoded.outPathLength) *
            static_cast<std::size_t>(decoded.outPathHashMode + 1);
        if (pathByteCount > 64)
            return false;
        decoded.outPathHex.reserve(pathByteCount * 2);
        for (std::size_t i = 0; i < pathByteCount; ++i) {
            const std::uint8_t byte = packet[36 + i];
            decoded.outPathHex.push_back(hex[byte >> 4]);
            decoded.outPathHex.push_back(hex[byte & 0x0f]);
        }
    }

    for (std::size_t i = 100; i < 132; ++i)
        if (packet[i] != 0)
            decoded.advertisedName.push_back(static_cast<char>(packet[i]));

    const auto readLE32 = [&packet](std::size_t offset) {
        return static_cast<std::uint32_t>(packet[offset]) |
               (static_cast<std::uint32_t>(packet[offset + 1]) << 8) |
               (static_cast<std::uint32_t>(packet[offset + 2]) << 16) |
               (static_cast<std::uint32_t>(packet[offset + 3]) << 24);
    };
    const auto readSignedLE32 = [&readLE32](std::size_t offset) {
        const std::uint32_t value = readLE32(offset);
        if (value <= static_cast<std::uint32_t>(
                         std::numeric_limits<std::int32_t>::max()))
            return static_cast<std::int32_t>(value);
        return static_cast<std::int32_t>(
            -1 - static_cast<std::int32_t>(~value));
    };
    decoded.lastAdvert = readLE32(132);
    decoded.latitudeMicrodegrees = readSignedLE32(136);
    decoded.longitudeMicrodegrees = readSignedLE32(140);
    decoded.lastModified = readLE32(144);

    result = decoded;
    return true;
}

bool MeshCoreCompanionCodec::decodeSelfInfo(
    const std::vector<std::uint8_t> &packet, MeshCoreSelfInfo &result)
{
    result = MeshCoreSelfInfo();
    if (packet.size() < 58 || packet[0] != 5)
        return false;

    const auto readLE32 = [&packet](std::size_t offset) {
        return static_cast<std::uint32_t>(packet[offset]) |
               (static_cast<std::uint32_t>(packet[offset + 1]) << 8) |
               (static_cast<std::uint32_t>(packet[offset + 2]) << 16) |
               (static_cast<std::uint32_t>(packet[offset + 3]) << 24);
    };
    const auto readSignedLE32 = [&readLE32](std::size_t offset) {
        const std::uint32_t value = readLE32(offset);
        if (value <= static_cast<std::uint32_t>(
                         std::numeric_limits<std::int32_t>::max()))
            return static_cast<std::int32_t>(value);
        return static_cast<std::int32_t>(
            -1 - static_cast<std::int32_t>(~value));
    };

    MeshCoreSelfInfo decoded;
    decoded.advertType = packet[1];
    decoded.txPower = packet[2];
    decoded.maxTxPower = packet[3];
    static const char hex[] = "0123456789abcdef";
    decoded.publicKeyHex.reserve(64);
    for (std::size_t i = 4; i < 36; ++i) {
        decoded.publicKeyHex.push_back(hex[packet[i] >> 4]);
        decoded.publicKeyHex.push_back(hex[packet[i] & 0x0f]);
    }
    decoded.latitudeMicrodegrees = readSignedLE32(36);
    decoded.longitudeMicrodegrees = readSignedLE32(40);
    decoded.multiAcks = packet[44];
    decoded.advertLocationPolicy = packet[45];
    decoded.telemetryMode = packet[46];
    decoded.telemetryEnvironment = (decoded.telemetryMode >> 4) & 0x03;
    decoded.telemetryLocation = (decoded.telemetryMode >> 2) & 0x03;
    decoded.telemetryBase = decoded.telemetryMode & 0x03;
    decoded.manualAddContacts = packet[47] > 0;
    decoded.radioFrequencyHz = readLE32(48);
    decoded.radioBandwidthHz = readLE32(52);
    decoded.radioSpreadingFactor = packet[56];
    decoded.radioCodingRate = packet[57];
    decoded.name.assign(packet.begin() + 58, packet.end());

    result = decoded;
    return true;
}

bool MeshCoreCompanionCodec::decodeMessageSent(
    const std::vector<std::uint8_t> &packet, MeshCoreMessageSent &result)
{
    result = MeshCoreMessageSent();
    if (packet.size() < 10 || packet[0] != 6)
        return false;

    static const char hex[] = "0123456789abcdef";
    MeshCoreMessageSent decoded;
    decoded.type = packet[1];
    decoded.expectedAckHex.reserve(8);
    for (std::size_t i = 2; i < 6; ++i) {
        decoded.expectedAckHex.push_back(hex[packet[i] >> 4]);
        decoded.expectedAckHex.push_back(hex[packet[i] & 0x0f]);
    }
    decoded.suggestedTimeoutMs =
        static_cast<std::uint32_t>(packet[6]) |
        (static_cast<std::uint32_t>(packet[7]) << 8) |
        (static_cast<std::uint32_t>(packet[8]) << 16) |
        (static_cast<std::uint32_t>(packet[9]) << 24);

    result = decoded;
    return true;
}

bool MeshCoreCompanionCodec::decodeAck(
    const std::vector<std::uint8_t> &packet, MeshCoreAck &result)
{
    result = MeshCoreAck();
    if (packet.empty() || packet[0] != 0x82)
        return false;
    if ((packet.size() > 1 && packet.size() < 5) ||
        (packet.size() > 5 && packet.size() < 9))
        return false;

    MeshCoreAck decoded;
    if (packet.size() >= 5) {
        static const char hex[] = "0123456789abcdef";
        decoded.hasCode = true;
        decoded.codeHex.reserve(8);
        for (std::size_t i = 1; i < 5; ++i) {
            decoded.codeHex.push_back(hex[packet[i] >> 4]);
            decoded.codeHex.push_back(hex[packet[i] & 0x0f]);
        }
    }
    if (packet.size() >= 9) {
        decoded.hasTripTime = true;
        decoded.tripTimeMs =
            static_cast<std::uint32_t>(packet[5]) |
            (static_cast<std::uint32_t>(packet[6]) << 8) |
            (static_cast<std::uint32_t>(packet[7]) << 16) |
            (static_cast<std::uint32_t>(packet[8]) << 24);
    }

    result = decoded;
    return true;
}

bool MeshCoreCompanionCodec::decodeContactListStart(
    const std::vector<std::uint8_t> &packet, std::uint32_t &contactCount)
{
    contactCount = 0;
    if (packet.size() < 5 || packet[0] != 2)
        return false;

    contactCount = static_cast<std::uint32_t>(packet[1]) |
                   (static_cast<std::uint32_t>(packet[2]) << 8) |
                   (static_cast<std::uint32_t>(packet[3]) << 16) |
                   (static_cast<std::uint32_t>(packet[4]) << 24);
    return true;
}

bool MeshCoreCompanionCodec::decodeContactListEnd(
    const std::vector<std::uint8_t> &packet, std::uint32_t &lastModified)
{
    lastModified = 0;
    if (packet.size() < 5 || packet[0] != 4)
        return false;

    lastModified = static_cast<std::uint32_t>(packet[1]) |
                   (static_cast<std::uint32_t>(packet[2]) << 8) |
                   (static_cast<std::uint32_t>(packet[3]) << 16) |
                   (static_cast<std::uint32_t>(packet[4]) << 24);
    return true;
}

std::vector<std::vector<std::uint8_t>> MeshCoreFrameParser::feed(
    const std::vector<std::uint8_t> &input)
{
    buffer.insert(buffer.end(), input.begin(), input.end());
    std::vector<std::vector<std::uint8_t>> frames;

    for (;;) {
        const auto marker = std::find(buffer.begin(), buffer.end(),
                                      static_cast<std::uint8_t>('>'));
        if (marker == buffer.end()) {
            buffer.clear();
            return frames;
        }
        buffer.erase(buffer.begin(), marker);

        if (buffer.size() < 3)
            return frames;

        const std::size_t length = static_cast<std::size_t>(buffer[1]) |
                                   (static_cast<std::size_t>(buffer[2]) << 8);
        if (length == 0 || length > 300) {
            buffer.erase(buffer.begin());
            continue;
        }
        const std::size_t frameSize = length + 3;
        if (buffer.size() < frameSize)
            return frames;

        frames.emplace_back(buffer.begin() + 3,
                            buffer.begin() + static_cast<std::ptrdiff_t>(frameSize));
        buffer.erase(buffer.begin(),
                     buffer.begin() + static_cast<std::ptrdiff_t>(frameSize));
    }
}

void MeshCoreFrameParser::reset()
{
    buffer.clear();
}
