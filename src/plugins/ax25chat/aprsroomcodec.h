#ifndef APRSROOMCODEC_H
#define APRSROOMCODEC_H

#include <QByteArray>
#include <QString>

namespace AprsRoom
{
struct RoomMessage
{
    QString roomName;
    QString body;
};

class Codec
{
public:
    // Decode an APRS group message into components
    static bool decode(const QByteArray &encoded, RoomMessage *message, QString *error = nullptr);
    
    // Encode components into an APRS group message packet
    static bool encode(const RoomMessage &message, QByteArray *encoded, QString *error = nullptr);
    
    // Encode ANSRVR subscription commands addressed to the group service.
    static bool encodeJoinCommand(const QString &roomName, QByteArray *encoded, QString *error = nullptr);
    static bool encodeLeaveCommand(const QString &roomName, QByteArray *encoded, QString *error = nullptr);

    // Validate a room name string
    static bool validateRoomName(const QString &roomName, QString *error = nullptr);
};
} // namespace AprsRoom

#endif // APRSROOMCODEC_H