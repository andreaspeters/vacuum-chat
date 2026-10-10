#ifndef AXCPCODEC_H
#define AXCPCODEC_H

#include <QByteArray>
#include <QString>
#include <QtGlobal>

namespace Axcp
{
enum class Type : quint8
{
    Hello = 0x01,
    Message = 0x02,
    Ack = 0x03,
    Nack = 0x04,
    Ping = 0x05,
    Pong = 0x06,
    Join = 0x07,
    Leave = 0x08,
    Presence = 0x09,
    Info = 0x0a
};

struct Packet
{
    Type type = Type::Message;
    quint8 flags = 0;
    quint32 messageId = 0;
    quint16 sequence = 0;
    quint8 fragmentIndex = 0;
    quint8 fragmentCount = 0;
    QByteArray payload;
};

class Codec
{
public:
    static const quint8 Version = 1;
    static const int HeaderSize = 9;
    static const int DefaultMaximumPayloadBytes = 180;

    static const quint8 FragmentedFlag = 0x01;
    static const quint8 FinalFragmentFlag = 0x02;
    static const quint8 RelayedFlag = 0x04;
    static const quint8 AckRequiredFlag = 0x08;
    static const quint8 ChannelMessageFlag = 0x10;

    // A fragmented packet carries fragmentIndex and fragmentCount directly
    // after the fixed nine-byte header, followed by a UTF-8 payload.
    static bool encode(const Packet &packet, QByteArray *encoded,
                       QString *error = nullptr,
                       int maximumPayloadBytes = DefaultMaximumPayloadBytes);
    static bool decode(const QByteArray &encoded, Packet *packet,
                       QString *error = nullptr,
                       int maximumPayloadBytes = DefaultMaximumPayloadBytes);
};
}

#endif // AXCPCODEC_H
