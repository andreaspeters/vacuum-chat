#ifndef APRSMESSAGECODEC_H
#define APRSMESSAGECODEC_H

#include <QByteArray>
#include <QString>

namespace AprsMessage
{
struct Message
{
    bool isAck = false;
    QString destination;
    QString body;
    QString messageId;
};

class Codec
{
public:
    // Decode an APRS message packet into components
    static bool decode(const QByteArray &encoded, Message *message, QString *error = nullptr);
    
    // Encode components into an APRS message packet
    static bool encode(const Message &message, QByteArray *encoded, QString *error = nullptr);
};
} // namespace AprsMessage

#endif // APRSMESSAGECODEC_H