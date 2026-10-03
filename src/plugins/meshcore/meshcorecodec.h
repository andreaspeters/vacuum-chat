#ifndef MESH_CORE_CODEC_H
#define MESH_CORE_CODEC_H

#include <cstddef>
#include <cstdint>
#include <vector>
#include <string>

struct MeshCoreIncomingText
{
    bool isChannel = false;
    std::string senderPrefixHex;
    std::uint8_t channelIndex = 0;
    // The raw Companion path-length byte and its decoded mode/hop count.
    std::uint8_t pathLength = 0;
    int pathHashMode = 0;
    int pathLengthHops = 0;
    std::uint8_t textType = 0;
    std::uint32_t senderTimestamp = 0;
    bool hasSnr = false;
    std::int8_t snrQuarterDb = 0;
    std::string signatureHex;
    std::string text;
    bool hasSignature = false;
};

struct MeshCoreContactRecord
{
    std::string publicKeyHex;
    std::uint8_t type = 0;
    std::uint8_t flags = 0;
    int outPathHashMode = 0;
    int outPathLength = 0;
    std::string outPathHex;
    std::string advertisedName;
    std::uint32_t lastAdvert = 0;
    std::int32_t latitudeMicrodegrees = 0;
    std::int32_t longitudeMicrodegrees = 0;
    std::uint32_t lastModified = 0;
};

struct MeshCoreSelfInfo
{
    std::uint8_t advertType = 0;
    std::uint8_t txPower = 0;
    std::uint8_t maxTxPower = 0;
    std::string publicKeyHex;
    std::int32_t latitudeMicrodegrees = 0;
    std::int32_t longitudeMicrodegrees = 0;
    std::uint8_t multiAcks = 0;
    std::uint8_t advertLocationPolicy = 0;
    std::uint8_t telemetryMode = 0;
    std::uint8_t telemetryEnvironment = 0;
    std::uint8_t telemetryLocation = 0;
    std::uint8_t telemetryBase = 0;
    bool manualAddContacts = false;
    std::uint32_t radioFrequencyHz = 0;
    std::uint32_t radioBandwidthHz = 0;
    std::uint8_t radioSpreadingFactor = 0;
    std::uint8_t radioCodingRate = 0;
    std::string name;
};

struct MeshCoreMessageSent
{
    std::uint8_t type = 0;
    std::string expectedAckHex;
    std::uint32_t suggestedTimeoutMs = 0;
};

struct MeshCoreAck
{
    bool hasCode = false;
    std::string codeHex;
    bool hasTripTime = false;
    std::uint32_t tripTimeMs = 0;
};

class MeshCoreCompanionCodec
{
public:
    // USB app->radio frame: '<' + uint16 little-endian length + payload.
    static std::vector<std::uint8_t> encodeFrame(const std::vector<std::uint8_t> &payload);

    // BLE companion payloads are not wrapped in the USB framing.
    static std::vector<std::uint8_t> encodeBLEPayload(const std::vector<std::uint8_t> &payload);
    static std::vector<std::uint8_t> decodeBLEPayload(const std::vector<std::uint8_t> &payload);
    
    // Direct text command payload: 0x02, 0x00, attempt, timestamp(uint32 LE), destination(6 bytes), UTF-8 text
    static std::vector<std::uint8_t> encodeDirectTextCommand(std::uint8_t attempt, std::uint32_t timestamp, 
                                                             const std::vector<std::uint8_t> &destination, 
                                                             const std::string &text);
    
    // Channel text command payload: 0x03, 0x00, channel index, timestamp(uint32 LE), UTF-8 text
    static std::vector<std::uint8_t> encodeChannelTextCommand(std::uint8_t channelIndex, std::uint32_t timestamp, 
                                                              const std::string &text);
    
    // SET_CHANNEL payload: command, channel index, 32-byte UTF-8 name, and 16-byte secret.
    static std::vector<std::uint8_t> encodeSetChannel(
        std::uint8_t channelIndex, const std::string &name,
        const std::vector<std::uint8_t> &secret);

    // ADD_UPDATE_CONTACT payload: command, 32-byte public key, contact metadata,
    // 64-byte path, 32-byte name, last-advert time, and GPS coordinates.
    // The optional last-modified field is omitted so firmware supplies its own time.
    static std::vector<std::uint8_t> encodeAddUpdateContact(
        const std::string &publicKeyHex, std::uint8_t type, std::uint8_t flags,
        int outPathHashMode, int outPathLength, const std::string &outPathHex,
        const std::string &name, std::uint32_t lastAdvert,
        std::int32_t latitudeMicrodegrees, std::int32_t longitudeMicrodegrees);

    // Decode a Companion direct or channel message (types 7, 8, 16, or 17).
    // On failure, returns false and resets result to its default state.
    static bool decodeIncomingText(const std::vector<std::uint8_t> &packet,
                                   MeshCoreIncomingText &result);
    static bool decodeContactRecord(const std::vector<std::uint8_t> &packet,
                                    MeshCoreContactRecord &result);
    static bool decodeSelfInfo(const std::vector<std::uint8_t> &packet,
                               MeshCoreSelfInfo &result);
    static bool decodeMessageSent(const std::vector<std::uint8_t> &packet,
                                  MeshCoreMessageSent &result);
    static bool decodeAck(const std::vector<std::uint8_t> &packet,
                          MeshCoreAck &result);
    static bool decodeContactListStart(const std::vector<std::uint8_t> &packet,
                                       std::uint32_t &contactCount);
    static bool decodeContactListEnd(const std::vector<std::uint8_t> &packet,
                                     std::uint32_t &lastModified);
};

class MeshCoreFrameParser
{
public:
    // Feed arbitrary radio->app bytes. Complete payloads are returned in order.
    // Leading junk is discarded until the '>' radio frame marker is found.
    std::vector<std::vector<std::uint8_t>> feed(const std::vector<std::uint8_t> &input);
    void reset();

private:
    std::vector<std::uint8_t> buffer;
};

#endif // MESH_CORE_CODEC_H
