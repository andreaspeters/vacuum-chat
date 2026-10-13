#include "aprsroomcodec.h"

#include <QChar>
#include <QList>

namespace
{
constexpr int MaximumRoomNameCharacters = 46;
constexpr int MaximumBodyCharacters = 67;

bool fail(QString *error, const QString &text)
{
    if (error)
        *error = text;
    return false;
}

bool isRoomName(const QString &name)
{
    if (name.isEmpty() || name.size() > MaximumRoomNameCharacters)
        return false;
    for (const QChar c : name) {
        const ushort u = c.unicode();
        if (!((u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') ||
              (u >= '0' && u <= '9') || u == '-' || u == '_'))
            return false;
    }
    return true;
}

bool isValidText(const QString &text)
{
    if (text.trimmed().isEmpty() || text.toUcs4().size() > MaximumBodyCharacters)
        return false;
    for (uint c : text.toUcs4()) {
        if (c < 0x20 || (c >= 0x7f && c <= 0x9f) ||
            c == 0x2028 || c == 0x2029)
            return false;
    }
    return true;
}

bool fromUtf8Strict(const QByteArray &bytes, QString *text)
{
    const QString decoded = QString::fromUtf8(bytes.constData(), bytes.size());
    if (decoded.toUtf8() != bytes)
        return false;
    *text = decoded;
    return true;
}
}

namespace AprsRoom
{
bool Codec::validateRoomName(const QString &roomName, QString *error)
{
    if (error)
        error->clear();
    if (roomName.isEmpty())
        return fail(error, QStringLiteral("APRS room name is empty"));
    if (!isRoomName(roomName))
        return fail(error, QStringLiteral("APRS room name must be 1-46 ASCII alphanumeric, hyphen or underscore characters"));
    return true;
}

bool Codec::encodeJoinCommand(const QString &roomName, QByteArray *encoded, QString *error)
{
    if (error)
        error->clear();
    if (!encoded)
        return fail(error, QStringLiteral("APRS encoded output is null"));
    encoded->clear();
    const QString canonicalName = roomName.trimmed().toUpper();
    if (!validateRoomName(canonicalName, error))
        return false;
    *encoded = QByteArrayLiteral("J ") + canonicalName.toLatin1();
    return true;
}

bool Codec::encodeLeaveCommand(const QString &roomName, QByteArray *encoded, QString *error)
{
    if (error)
        error->clear();
    if (!encoded)
        return fail(error, QStringLiteral("APRS encoded output is null"));
    encoded->clear();
    const QString canonicalName = roomName.trimmed().toUpper();
    if (!validateRoomName(canonicalName, error))
        return false;
    *encoded = QByteArrayLiteral("U ") + canonicalName.toLatin1();
    return true;
}

bool Codec::encode(const RoomMessage &message, QByteArray *encoded, QString *error)
{
    if (error)
        error->clear();
    if (!encoded)
        return fail(error, QStringLiteral("APRS encoded output is null"));
    encoded->clear();
    
    const QString canonicalName = message.roomName.trimmed().toUpper();
    if (!validateRoomName(canonicalName, error))
        return false;
    
    if (!isValidText(message.body))
        return fail(error, QStringLiteral("APRS room message body must contain 1-67 printable characters"));

    const int prefixLength = 4 + canonicalName.size(); // "CQ " + room + " "
    if (prefixLength + message.body.toUcs4().size() > MaximumBodyCharacters)
        return fail(error, QStringLiteral("APRS group message exceeds the 67-character text limit"));
        
    // Format: CQ <roomName> <body>
    QByteArray result = QByteArrayLiteral("CQ ") + canonicalName.toLatin1() +
        QByteArrayLiteral(" ") + message.body.toUtf8();
    
    *encoded = result;
    return true;
}

bool Codec::decode(const QByteArray &encoded, RoomMessage *message, QString *error)
{
    if (error)
        error->clear();
    if (!message)
        return fail(error, QStringLiteral("APRS decoded-message output is null"));
    *message = RoomMessage();
    
    QString str;
    if (!fromUtf8Strict(encoded, &str))
        return fail(error, QStringLiteral("APRS group message is not valid UTF-8"));
    if (str.left(3).compare(QStringLiteral("CQ "), Qt::CaseInsensitive) != 0)
        return fail(error, QStringLiteral("APRS group message must start with \"CQ \" prefix"));
    
    const int space1 = str.indexOf(QLatin1Char(' '), 3);
    if (space1 < 0)
        return fail(error, QStringLiteral("APRS group message missing room name"));
    
    QString roomName = str.mid(3, space1 - 3).trimmed().toUpper();
    if (!validateRoomName(roomName, error))
        return false;
        
    const QString body = str.mid(space1 + 1);
    if (!isValidText(body))
        return fail(error, QStringLiteral("APRS room message body is empty or too long"));
    
    message->roomName = roomName;
    message->body = body;
    return true;
}
} // namespace AprsRoom