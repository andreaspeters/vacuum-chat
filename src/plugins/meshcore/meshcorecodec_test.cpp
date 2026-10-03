#include "meshcorecodec.h"

#include <iostream>
#include <string>
#include <vector>

namespace
{
bool check(const char *name, bool condition)
{
    if (condition)
        return true;
    std::cerr << name << " failed\n";
    return false;
}

bool matches(const MeshCoreIncomingText &actual,
             const MeshCoreIncomingText &expected)
{
    return actual.isChannel == expected.isChannel &&
           actual.senderPrefixHex == expected.senderPrefixHex &&
           actual.channelIndex == expected.channelIndex &&
           actual.pathLength == expected.pathLength &&
           actual.pathHashMode == expected.pathHashMode &&
           actual.pathLengthHops == expected.pathLengthHops &&
           actual.textType == expected.textType &&
           actual.senderTimestamp == expected.senderTimestamp &&
           actual.hasSnr == expected.hasSnr &&
           actual.snrQuarterDb == expected.snrQuarterDb &&
           actual.signatureHex == expected.signatureHex &&
           actual.text == expected.text &&
           actual.hasSignature == expected.hasSignature;
}

bool decodeMatches(const char *name, const std::vector<std::uint8_t> &packet,
                   const MeshCoreIncomingText &expected)
{
    MeshCoreIncomingText actual;
    const bool decoded = MeshCoreCompanionCodec::decodeIncomingText(packet, actual);
    return check(name, decoded && matches(actual, expected));
}

bool decodeRejects(const char *name, const std::vector<std::uint8_t> &packet)
{
    MeshCoreIncomingText actual;
    actual.text = "stale";
    const bool decoded = MeshCoreCompanionCodec::decodeIncomingText(packet, actual);
    return check(name, !decoded && matches(actual, MeshCoreIncomingText()));
}

bool contactMatches(const MeshCoreContactRecord &actual,
                    const MeshCoreContactRecord &expected)
{
    return actual.publicKeyHex == expected.publicKeyHex &&
           actual.type == expected.type && actual.flags == expected.flags &&
           actual.outPathHashMode == expected.outPathHashMode &&
           actual.outPathLength == expected.outPathLength &&
           actual.outPathHex == expected.outPathHex &&
           actual.advertisedName == expected.advertisedName &&
           actual.lastAdvert == expected.lastAdvert &&
           actual.latitudeMicrodegrees == expected.latitudeMicrodegrees &&
           actual.longitudeMicrodegrees == expected.longitudeMicrodegrees &&
           actual.lastModified == expected.lastModified;
}

bool decodeContactMatches(const char *name,
                          const std::vector<std::uint8_t> &packet,
                          const MeshCoreContactRecord &expected)
{
    MeshCoreContactRecord actual;
    const bool decoded = MeshCoreCompanionCodec::decodeContactRecord(packet, actual);
    return check(name, decoded && contactMatches(actual, expected));
}

bool decodeContactRejects(const char *name,
                          const std::vector<std::uint8_t> &packet)
{
    MeshCoreContactRecord actual;
    actual.publicKeyHex = "stale";
    const bool decoded = MeshCoreCompanionCodec::decodeContactRecord(packet, actual);
    return check(name, !decoded && contactMatches(actual, MeshCoreContactRecord()));
}

bool selfInfoMatches(const MeshCoreSelfInfo &actual,
                     const MeshCoreSelfInfo &expected)
{
    return actual.advertType == expected.advertType &&
           actual.txPower == expected.txPower &&
           actual.maxTxPower == expected.maxTxPower &&
           actual.publicKeyHex == expected.publicKeyHex &&
           actual.latitudeMicrodegrees == expected.latitudeMicrodegrees &&
           actual.longitudeMicrodegrees == expected.longitudeMicrodegrees &&
           actual.multiAcks == expected.multiAcks &&
           actual.advertLocationPolicy == expected.advertLocationPolicy &&
           actual.telemetryMode == expected.telemetryMode &&
           actual.telemetryEnvironment == expected.telemetryEnvironment &&
           actual.telemetryLocation == expected.telemetryLocation &&
           actual.telemetryBase == expected.telemetryBase &&
           actual.manualAddContacts == expected.manualAddContacts &&
           actual.radioFrequencyHz == expected.radioFrequencyHz &&
           actual.radioBandwidthHz == expected.radioBandwidthHz &&
           actual.radioSpreadingFactor == expected.radioSpreadingFactor &&
           actual.radioCodingRate == expected.radioCodingRate &&
           actual.name == expected.name;
}

bool decodeSelfInfoMatches(const char *name,
                           const std::vector<std::uint8_t> &packet,
                           const MeshCoreSelfInfo &expected)
{
    MeshCoreSelfInfo actual;
    const bool decoded = MeshCoreCompanionCodec::decodeSelfInfo(packet, actual);
    return check(name, decoded && selfInfoMatches(actual, expected));
}

bool decodeSelfInfoRejects(const char *name,
                           const std::vector<std::uint8_t> &packet)
{
    MeshCoreSelfInfo actual;
    actual.name = "stale";
    const bool decoded = MeshCoreCompanionCodec::decodeSelfInfo(packet, actual);
    return check(name, !decoded && selfInfoMatches(actual, MeshCoreSelfInfo()));
}

bool decodeMessageSentMatches(const char *name,
                              const std::vector<std::uint8_t> &packet,
                              const MeshCoreMessageSent &expected)
{
    MeshCoreMessageSent actual;
    const bool decoded = MeshCoreCompanionCodec::decodeMessageSent(packet, actual);
    return check(name, decoded && actual.type == expected.type &&
           actual.expectedAckHex == expected.expectedAckHex &&
           actual.suggestedTimeoutMs == expected.suggestedTimeoutMs);
}

bool decodeMessageSentRejects(const char *name,
                              const std::vector<std::uint8_t> &packet)
{
    MeshCoreMessageSent actual;
    actual.expectedAckHex = "stale";
    const bool decoded = MeshCoreCompanionCodec::decodeMessageSent(packet, actual);
    const MeshCoreMessageSent empty;
    return check(name, !decoded && actual.type == empty.type &&
           actual.expectedAckHex == empty.expectedAckHex &&
           actual.suggestedTimeoutMs == empty.suggestedTimeoutMs);
}

bool decodeAckMatches(const char *name, const std::vector<std::uint8_t> &packet,
                      const MeshCoreAck &expected)
{
    MeshCoreAck actual;
    const bool decoded = MeshCoreCompanionCodec::decodeAck(packet, actual);
    return check(name, decoded && actual.hasCode == expected.hasCode &&
           actual.codeHex == expected.codeHex &&
           actual.hasTripTime == expected.hasTripTime &&
           actual.tripTimeMs == expected.tripTimeMs);
}

bool decodeAckRejects(const char *name, const std::vector<std::uint8_t> &packet)
{
    MeshCoreAck actual;
    actual.codeHex = "stale";
    const bool decoded = MeshCoreCompanionCodec::decodeAck(packet, actual);
    const MeshCoreAck empty;
    return check(name, !decoded && actual.hasCode == empty.hasCode &&
           actual.codeHex == empty.codeHex &&
           actual.hasTripTime == empty.hasTripTime &&
           actual.tripTimeMs == empty.tripTimeMs);
}
}

int main()
{
    bool passed = true;

    const std::vector<std::uint8_t> destination = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06
    };
    const std::vector<std::uint8_t> directCommand = {
        0x02, 0x00, 0x12, 0x78, 0x56, 0x34, 0x12,
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 'H', 'i'
    };
    passed &= check("direct text command encoding",
        MeshCoreCompanionCodec::encodeDirectTextCommand(
            0x12, 0x12345678, destination, "Hi") == directCommand);
    const std::vector<std::uint8_t> channelCommand = {
        0x03, 0x00, 0x05, 0x21, 0x43, 0x65, 0x87, 'O', 'K'
    };
    passed &= check("channel text command encoding",
        MeshCoreCompanionCodec::encodeChannelTextCommand(
            0x05, 0x87654321, "OK") == channelCommand);
    passed &= check("invalid direct destination length",
        MeshCoreCompanionCodec::encodeDirectTextCommand(
            0, 1, {0x01, 0x02, 0x03}, "Hi").empty());

    std::vector<std::uint8_t> contactPacket = {3};
    for (std::uint8_t byte = 0; byte < 32; ++byte)
        contactPacket.push_back(byte);
    contactPacket.insert(contactPacket.end(), {0x02, 0x04, 0x41, 0xab, 0xcd});
    contactPacket.insert(contactPacket.end(), 62, 0x00);
    contactPacket.insert(contactPacket.end(), {'N', 'o', 'd', 'e'});
    contactPacket.insert(contactPacket.end(), 28, 0x00);
    contactPacket.insert(contactPacket.end(), {
        0x78, 0x56, 0x34, 0x12, 0x01, 0x00, 0x00, 0x00,
        0xff, 0xff, 0xff, 0xff, 0xef, 0xcd, 0xab, 0x90
    });
    MeshCoreContactRecord contactExpected;
    contactExpected.publicKeyHex =
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
    contactExpected.type = 0x02;
    contactExpected.flags = 0x04;
    contactExpected.outPathHashMode = 1;
    contactExpected.outPathLength = 1;
    contactExpected.outPathHex = "abcd";
    contactExpected.advertisedName = "Node";
    contactExpected.lastAdvert = 0x12345678;
    contactExpected.latitudeMicrodegrees = 1;
    contactExpected.longitudeMicrodegrees = -1;
    contactExpected.lastModified = 0x90abcdef;
    passed &= decodeContactMatches("CONTACT record", contactPacket, contactExpected);

    std::vector<std::uint8_t> pushedContact = contactPacket;
    pushedContact[0] = 0x8a;
    passed &= decodeContactMatches("new-advert contact record", pushedContact,
                                   contactExpected);
    std::vector<std::uint8_t> malformedContact = contactPacket;
    malformedContact[35] = 0xd1; // 17 hops x 4-byte hashes exceed the fixed path field.
    passed &= decodeContactRejects("contact path length overflow", malformedContact);
    contactPacket.pop_back();
    passed &= decodeContactRejects("truncated contact record", contactPacket);

    std::uint32_t contactCount = 99;
    passed &= check("CONTACT_START count",
        MeshCoreCompanionCodec::decodeContactListStart({2, 3, 0, 0, 0},
                                                       contactCount) &&
        contactCount == 3);
    contactCount = 99;
    passed &= check("truncated CONTACT_START resets output",
        !MeshCoreCompanionCodec::decodeContactListStart({2, 3}, contactCount) &&
        contactCount == 0);
    std::uint32_t contactLastModified = 99;
    passed &= check("CONTACT_END last-modified value",
        MeshCoreCompanionCodec::decodeContactListEnd(
            {4, 0x34, 0x12, 0, 0}, contactLastModified) &&
        contactLastModified == 0x1234);
    contactLastModified = 99;
    passed &= check("wrong CONTACT_END type resets output",
        !MeshCoreCompanionCodec::decodeContactListEnd(
            {2, 0x34, 0x12, 0, 0}, contactLastModified) &&
        contactLastModified == 0);

    std::vector<std::uint8_t> selfInfoPacket = {5, 0x01, 0x10, 0x20};
    for (std::uint8_t byte = 0; byte < 32; ++byte)
        selfInfoPacket.push_back(byte);
    selfInfoPacket.insert(selfInfoPacket.end(), {
        0x01, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
        0x02, 0x03, 0x29, 0x01,
        0x12, 0x34, 0x56, 0x78, 0x21, 0x43, 0x65, 0x87,
        0x08, 0x05, 'R', 'a', 'd', 'i', 'o'
    });
    MeshCoreSelfInfo selfInfoExpected;
    selfInfoExpected.advertType = 0x01;
    selfInfoExpected.txPower = 0x10;
    selfInfoExpected.maxTxPower = 0x20;
    selfInfoExpected.publicKeyHex = contactExpected.publicKeyHex;
    selfInfoExpected.latitudeMicrodegrees = 1;
    selfInfoExpected.longitudeMicrodegrees = -1;
    selfInfoExpected.multiAcks = 0x02;
    selfInfoExpected.advertLocationPolicy = 0x03;
    selfInfoExpected.telemetryMode = 0x29;
    selfInfoExpected.telemetryEnvironment = 2;
    selfInfoExpected.telemetryLocation = 2;
    selfInfoExpected.telemetryBase = 1;
    selfInfoExpected.manualAddContacts = true;
    selfInfoExpected.radioFrequencyHz = 0x78563412;
    selfInfoExpected.radioBandwidthHz = 0x87654321;
    selfInfoExpected.radioSpreadingFactor = 8;
    selfInfoExpected.radioCodingRate = 5;
    selfInfoExpected.name = "Radio";
    passed &= decodeSelfInfoMatches("SELF_INFO response", selfInfoPacket,
                                    selfInfoExpected);
    std::vector<std::uint8_t> truncatedSelfInfo = selfInfoPacket;
    truncatedSelfInfo.resize(57);
    passed &= decodeSelfInfoRejects("truncated SELF_INFO response",
                                    truncatedSelfInfo);
    std::vector<std::uint8_t> wrongSelfInfoType = selfInfoPacket;
    wrongSelfInfoType[0] = 6;
    passed &= decodeSelfInfoRejects("wrong SELF_INFO packet type",
                                    wrongSelfInfoType);

    const std::vector<std::uint8_t> messageSentPacket = {
        6, 0x02, 0xaa, 0xbb, 0xcc, 0xdd, 0x78, 0x56, 0x34, 0x12
    };
    MeshCoreMessageSent messageSentExpected;
    messageSentExpected.type = 0x02;
    messageSentExpected.expectedAckHex = "aabbccdd";
    messageSentExpected.suggestedTimeoutMs = 0x12345678;
    passed &= decodeMessageSentMatches("MSG_SENT acknowledgement",
                                       messageSentPacket, messageSentExpected);
    std::vector<std::uint8_t> truncatedMessageSent = messageSentPacket;
    truncatedMessageSent.pop_back();
    passed &= decodeMessageSentRejects("truncated MSG_SENT acknowledgement",
                                       truncatedMessageSent);
    std::vector<std::uint8_t> wrongMessageSentType = messageSentPacket;
    wrongMessageSentType[0] = 5;
    passed &= decodeMessageSentRejects("wrong MSG_SENT packet type",
                                       wrongMessageSentType);

    MeshCoreAck ackExpected;
    passed &= decodeAckMatches("empty ACK notification", {0x82}, ackExpected);
    ackExpected.hasCode = true;
    ackExpected.codeHex = "01020304";
    passed &= decodeAckMatches("ACK code", {0x82, 1, 2, 3, 4}, ackExpected);
    ackExpected.hasTripTime = true;
    ackExpected.tripTimeMs = 0x12345678;
    passed &= decodeAckMatches("ACK code and trip time",
        {0x82, 1, 2, 3, 4, 0x78, 0x56, 0x34, 0x12}, ackExpected);
    passed &= decodeAckRejects("truncated ACK code", {0x82, 1, 2, 3});
    passed &= decodeAckRejects("truncated ACK trip time",
                               {0x82, 1, 2, 3, 4, 0x78});
    passed &= decodeAckRejects("wrong ACK packet type", {0x06, 1, 2, 3, 4});

    const std::vector<std::uint8_t> directV2 = {
        7, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x05, 0x01,
        0x12, 0x34, 0x56, 0x78, 'T', 'e', 's', 't'
    };
    MeshCoreIncomingText directExpected;
    directExpected.senderPrefixHex = "112233445566";
    directExpected.pathLength = 0x05;
    directExpected.pathHashMode = 0;
    directExpected.pathLengthHops = 5;
    directExpected.textType = 0x01;
    directExpected.senderTimestamp = 0x78563412;
    directExpected.text = "Test";
    passed &= decodeMatches("CONTACT_MSG_RECV type 7", directV2, directExpected);

    const std::vector<std::uint8_t> signedDirectV2 = {
        7, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x05, 0x02,
        0x12, 0x34, 0x56, 0x78, 0xaa, 0xbb, 0xcc, 0xdd, 'S', 'i', 'g'
    };
    directExpected.text = "Sig";
    directExpected.textType = 0x02;
    directExpected.hasSignature = true;
    directExpected.signatureHex = "aabbccdd";
    passed &= decodeMatches("CONTACT_MSG_RECV signature", signedDirectV2, directExpected);

    const std::vector<std::uint8_t> directV3 = {
        16, 0x05, 0x00, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
        0x05, 0x01, 0x12, 0x34, 0x56, 0x78, 'V', '3'
    };
    directExpected.textType = 0x01;
    directExpected.text = "V3";
    directExpected.hasSignature = false;
    directExpected.signatureHex.clear();
    directExpected.hasSnr = true;
    directExpected.snrQuarterDb = 5;
    passed &= decodeMatches("CONTACT_MSG_RECV_V3 type 16", directV3, directExpected);

    const std::vector<std::uint8_t> signedDirectV3 = {
        16, 0x05, 0x00, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
        0x05, 0x02, 0x12, 0x34, 0x56, 0x78, 0xaa, 0xbb, 0xcc, 0xdd, 'V', '3'
    };
    directExpected.textType = 0x02;
    directExpected.text = "V3";
    directExpected.hasSignature = true;
    directExpected.signatureHex = "aabbccdd";
    passed &= decodeMatches("CONTACT_MSG_RECV_V3 signature", signedDirectV3, directExpected);

    const std::vector<std::uint8_t> channelV2 = {
        8, 0x03, 0x05, 0x01, 0x12, 0x34, 0x56, 0x78,
        0x00, 'T', 'e', 's', 't', 0x00
    };
    MeshCoreIncomingText channelExpected;
    channelExpected.isChannel = true;
    channelExpected.channelIndex = 0x03;
    channelExpected.pathLength = 0x05;
    channelExpected.pathHashMode = 0;
    channelExpected.pathLengthHops = 5;
    channelExpected.textType = 0x01;
    channelExpected.senderTimestamp = 0x78563412;
    channelExpected.text = "Test";
    passed &= decodeMatches("CHANNEL_MSG_RECV type 8 and NUL padding",
                            channelV2, channelExpected);

    const std::vector<std::uint8_t> channelV3 = {
        17, 0x05, 0x00, 0x00, 0x03, 0x05, 0x01,
        0x12, 0x34, 0x56, 0x78, 0x00, 'V', '3', 0x00
    };
    channelExpected.text = std::string("\0V3\0", 4);
    channelExpected.hasSnr = true;
    channelExpected.snrQuarterDb = 5;
    passed &= decodeMatches("CHANNEL_MSG_RECV_V3 type 17",
                            channelV3, channelExpected);

    passed &= decodeRejects("truncated direct header", {7, 0x11, 0x22});
    passed &= decodeRejects("truncated direct signature",
        {7, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x05, 0x02,
         0x12, 0x34, 0x56, 0x78, 0xaa, 0xbb, 0xcc});
    passed &= decodeRejects("truncated V3 channel header", {17, 0x05, 0x00});
    passed &= decodeRejects("unsupported packet type", {99, 0x00, 0x00, 0x00});

    MeshCoreFrameParser parser;
    passed &= check("fragmented frame header is buffered",
                    parser.feed({'>', 0x03}).empty());
    passed &= check("fragmented frame payload is buffered",
                    parser.feed({0x00, 7, 0x11}).empty());
    const std::vector<std::vector<std::uint8_t>> completedFrames =
        parser.feed({0x22, 0x33});
    passed &= check("fragmented frame is reassembled",
                    completedFrames.size() == 1 &&
                    completedFrames[0] == std::vector<std::uint8_t>({7, 0x11, 0x22}));

    const std::vector<std::vector<std::uint8_t>> framesWithJunk =
        parser.feed({'x', '>', 1, 0, 99, '>', 1, 0, 7});
    passed &= check("multiple frames resynchronize after junk",
                    framesWithJunk.size() == 2 &&
                    framesWithJunk[0] == std::vector<std::uint8_t>({99}) &&
                    framesWithJunk[1] == std::vector<std::uint8_t>({7}));
    MeshCoreFrameParser invalidLengthParser;
    const std::vector<std::vector<std::uint8_t>> recoveredFrames =
        invalidLengthParser.feed({'>', 0xff, 0xff, 0x00, '>', 1, 0, 7});
    passed &= check("oversize frame is discarded and parser resynchronizes",
                    recoveredFrames.size() == 1 &&
                    recoveredFrames[0] == std::vector<std::uint8_t>({7}));
    const std::vector<std::vector<std::uint8_t>> recoveredAfterEmpty =
        invalidLengthParser.feed({'>', 0, 0, '>', 1, 0, 8});
    passed &= check("empty frame is discarded and parser resynchronizes",
                    recoveredAfterEmpty.size() == 1 &&
                    recoveredAfterEmpty[0] == std::vector<std::uint8_t>({8}));
    MeshCoreIncomingText invalidFramedMessage;
    passed &= check("invalid framed response is rejected",
        framesWithJunk.size() == 2 &&
        !MeshCoreCompanionCodec::decodeIncomingText(
            framesWithJunk[0], invalidFramedMessage));

    const std::vector<std::uint8_t> privateSecret = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10
    };
    const std::vector<std::uint8_t> setChannel =
        MeshCoreCompanionCodec::encodeSetChannel(1, "TestChannel", privateSecret);
    passed &= check("SET_CHANNEL has exact command, slot, fixed name and 16-byte secret",
                    setChannel.size() == 50 && setChannel[0] == 0x20 &&
                    setChannel[1] == 1 &&
                    std::string(setChannel.begin() + 2, setChannel.begin() + 13) == "TestChannel" &&
                    setChannel[13] == 0 && setChannel[33] == 0 &&
                    std::vector<std::uint8_t>(setChannel.begin() + 34, setChannel.end()) == privateSecret);

    const std::vector<std::uint8_t> publicSecret = {
        0x8b, 0x33, 0x87, 0xe9, 0xc5, 0xcd, 0xea, 0x6a,
        0xc9, 0xe5, 0xed, 0xba, 0xa1, 0x15, 0xcd, 0x72
    };
    const std::vector<std::uint8_t> publicChannel =
        MeshCoreCompanionCodec::encodeSetChannel(0, "Public", publicSecret);
    passed &= check("SET_CHANNEL accepts the firmware public channel PSK",
                    publicChannel.size() == 50 && publicChannel[0] == 0x20 &&
                    publicChannel[1] == 0 && publicChannel[34] == 0x8b &&
                    publicChannel.back() == 0x72);
    passed &= check("SET_CHANNEL rejects an out-of-range slot",
                    MeshCoreCompanionCodec::encodeSetChannel(8, "Test", privateSecret).empty());
    passed &= check("SET_CHANNEL rejects a non-16-byte secret",
                    MeshCoreCompanionCodec::encodeSetChannel(1, "Test", {0x01, 0x02}).empty());
    passed &= check("SET_CHANNEL rejects a name too long for the fixed field",
                    MeshCoreCompanionCodec::encodeSetChannel(1, std::string(33, 'x'),
                                                            privateSecret).empty());
    passed &= check("SET_CHANNEL rejects embedded NUL in a channel name",
                    MeshCoreCompanionCodec::encodeSetChannel(1, std::string("A\0B", 3),
                                                            privateSecret).empty());

    const std::string publicKey =
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
    std::vector<std::uint8_t> addContactPacket = {0x09};
    for (std::uint8_t byte = 0; byte < 32; ++byte)
        addContactPacket.push_back(byte);
    addContactPacket.insert(addContactPacket.end(), {0x01, 0x00, 0x01, 0xab});
    addContactPacket.insert(addContactPacket.end(), 63, 0x00);
    addContactPacket.insert(addContactPacket.end(), {'T', 'e', 's', 't', 'N', 'a', 'm', 'e'});
    addContactPacket.insert(addContactPacket.end(), 24, 0x00);
    addContactPacket.insert(addContactPacket.end(), {
        0x78, 0x56, 0x34, 0x12, 0xff, 0xff, 0xff, 0xff,
        0x02, 0x00, 0x00, 0x00
    });
    const std::vector<std::uint8_t> encodedAddContact =
        MeshCoreCompanionCodec::encodeAddUpdateContact(
            publicKey, 0x01, 0x00, 0, 1, "ab", "TestName", 0x12345678, -1, 2);
    passed &= check("ADD_UPDATE_CONTACT encodes the fixed contact record fields",
                    encodedAddContact.size() == 144 && encodedAddContact == addContactPacket);

    std::vector<std::uint8_t> floodContactPacket = {0x09};
    for (std::uint8_t byte = 0; byte < 32; ++byte)
        floodContactPacket.push_back(byte);
    floodContactPacket.insert(floodContactPacket.end(), {0x01, 0x00, 0xff});
    floodContactPacket.insert(floodContactPacket.end(), 64, 0x00);
    floodContactPacket.insert(floodContactPacket.end(), {'T', 'e', 's', 't'});
    floodContactPacket.insert(floodContactPacket.end(), 28, 0x00);
    floodContactPacket.insert(floodContactPacket.end(), 12, 0x00);
    passed &= check("ADD_UPDATE_CONTACT encodes flood route with 64 zero path bytes",
                    MeshCoreCompanionCodec::encodeAddUpdateContact(
                        publicKey, 0x01, 0x00, -1, -1, "", "Test", 0, 0, 0) ==
                        floodContactPacket);

    passed &= check("ADD_UPDATE_CONTACT rejects an invalid public-key length",
                    MeshCoreCompanionCodec::encodeAddUpdateContact(
                        "010203", 0x01, 0, -1, -1, "", "Test", 0, 0, 0).empty());
    std::string invalidPublicKey = publicKey;
    invalidPublicKey[12] = 'g';
    passed &= check("ADD_UPDATE_CONTACT rejects non-hex public-key characters",
                    MeshCoreCompanionCodec::encodeAddUpdateContact(
                        invalidPublicKey, 0x01, 0, -1, -1, "", "Test", 0, 0, 0).empty());
    passed &= check("ADD_UPDATE_CONTACT rejects an invalid hash mode",
                    MeshCoreCompanionCodec::encodeAddUpdateContact(
                        publicKey, 0x01, 0, 4, 1, "ab", "Test", 0, 0, 0).empty());
    passed &= check("ADD_UPDATE_CONTACT rejects an invalid path length",
                    MeshCoreCompanionCodec::encodeAddUpdateContact(
                        publicKey, 0x01, 0, 0, 64, "", "Test", 0, 0, 0).empty());
    passed &= check("ADD_UPDATE_CONTACT rejects a path whose hex length mismatches hops",
                    MeshCoreCompanionCodec::encodeAddUpdateContact(
                        publicKey, 0x01, 0, 0, 2, "ab", "Test", 0, 0, 0).empty());
    passed &= check("ADD_UPDATE_CONTACT rejects non-hex path characters",
                    MeshCoreCompanionCodec::encodeAddUpdateContact(
                        publicKey, 0x01, 0, 0, 1, "gg", "Test", 0, 0, 0).empty());
    passed &= check("ADD_UPDATE_CONTACT rejects a partial flood-route sentinel",
                    MeshCoreCompanionCodec::encodeAddUpdateContact(
                        publicKey, 0x01, 0, -1, 1, "", "Test", 0, 0, 0).empty());
    passed &= check("ADD_UPDATE_CONTACT rejects names too long for a NUL-terminated field",
                    MeshCoreCompanionCodec::encodeAddUpdateContact(
                        publicKey, 0x01, 0, -1, -1, "", std::string(32, 'x'), 0, 0, 0).empty());
    passed &= check("ADD_UPDATE_CONTACT rejects embedded NUL in a contact name",
                    MeshCoreCompanionCodec::encodeAddUpdateContact(
                        publicKey, 0x01, 0, -1, -1, "", std::string("A\0B", 3), 0, 0, 0).empty());

    if (!passed)
        return 1;

    std::cout << "MeshCore codec tests passed\n";
    return 0;
}