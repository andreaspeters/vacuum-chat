#include "aprsmessagecodec.h"

#include <QChar>
#include <QList>

namespace
{
constexpr int AddresseeWidth = 9;
constexpr int MaximumBodyCharacters = 67;
constexpr int MaximumMessageIdCharacters = 5;

bool fail(QString *error, const QString &text)
{
    if (error)
        *error = text;
    return false;
}

bool isMessageId(const QString &id)
{
    if (id.isEmpty() || id.size() > MaximumMessageIdCharacters)
        return false;
    for (const QChar c : id) {
        if (!c.isLetterOrNumber() || c.unicode() > 0x7f)
            return false;
    }
    return true;
}

bool isAddress(const QString &address)
{
    if (address.isEmpty() || address.size() > AddresseeWidth)
        return false;
    for (const QChar c : address) {
        const ushort u = c.unicode();
        if (!((u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') ||
              (u >= '0' && u <= '9') || u == '-'))
            return false;
    }
    return true;
}

bool isValidText(const QString &text, bool hasId)
{
    if (text.isEmpty() || text.toUcs4().size() > MaximumBodyCharacters)
        return false;
    for (uint c : text.toUcs4()) {
        if (c < 0x20 || (c >= 0x7f && c <= 0x9f) ||
            c == 0x2028 || c == 0x2029 || (hasId && c == '{'))
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

bool parseId(const QString &id, QString *result, QString *error)
{
    if (!isMessageId(id))
        return fail(error, QStringLiteral("APRS message identifier must be 1-5 ASCII alphanumeric characters"));
    *result = id;
    return true;
}
}

namespace AprsMessage
{
bool Codec::encode(const Message &message, QByteArray *encoded, QString *error)
{
    if (error)
        error->clear();
    if (!encoded)
        return fail(error, QStringLiteral("APRS encoded output is null"));
    encoded->clear();
    if (!isAddress(message.destination))
        return fail(error, QStringLiteral("APRS destination must be 1-9 ASCII alphanumeric or hyphen characters"));

    QString id;
    QByteArray body;
    if (message.isAck) {
        if (!message.body.isEmpty())
            return fail(error, QStringLiteral("APRS ACK cannot contain a message body"));
        if (!parseId(message.messageId, &id, error))
            return false;
        body = QByteArrayLiteral("ack") + id.toLatin1();
    } else {
        if (!message.messageId.isEmpty() && !parseId(message.messageId, &id, error))
            return false;
        if (!isValidText(message.body, !id.isEmpty()))
            return fail(error, QStringLiteral("APRS message body must contain 1-67 printable characters"));
        body = message.body.toUtf8();
        if (!id.isEmpty())
            body += QByteArrayLiteral("{") + id.toLatin1();
    }

    QByteArray result;
    result.reserve(1 + AddresseeWidth + 1 + body.size());
    result.append(':');
    result.append(message.destination.toLatin1());
    result.append(QByteArray(AddresseeWidth - message.destination.size(), ' '));
    result.append(':');
    result.append(body);
    *encoded = result;
    return true;
}

bool Codec::decode(const QByteArray &encoded, Message *message, QString *error)
{
    if (error)
        error->clear();
    if (!message)
        return fail(error, QStringLiteral("APRS decoded-message output is null"));
    *message = Message();

    if (encoded.size() < 1 + AddresseeWidth + 1 || encoded.at(0) != ':' ||
        encoded.at(1 + AddresseeWidth) != ':')
        return fail(error, QStringLiteral("APRS message must contain a 9-character addressee field"));

    QString address;
    if (!fromUtf8Strict(encoded.mid(1, AddresseeWidth), &address))
        return fail(error, QStringLiteral("APRS addressee field is not valid UTF-8"));
    while (address.endsWith(QLatin1Char(' ')))
        address.chop(1);
    if (!isAddress(address))
        return fail(error, QStringLiteral("APRS addressee is malformed"));

    const QByteArray rawBody = encoded.mid(1 + AddresseeWidth + 1);
    QString body;
    if (!fromUtf8Strict(rawBody, &body))
        return fail(error, QStringLiteral("APRS message text is not valid UTF-8"));

    Message decoded;
    decoded.destination = address;
    if (body.startsWith(QStringLiteral("ack")) && isMessageId(body.mid(3))) {
        decoded.messageId = body.mid(3);
        decoded.isAck = true;
        *message = decoded;
        return true;
    }

    const int brace = body.lastIndexOf(QLatin1Char('{'));
    QString text = body;
    if (brace >= 0) {
        text = body.left(brace);
        if (!parseId(body.mid(brace + 1), &decoded.messageId, error))
            return false;
    }
    if (!isValidText(text, brace >= 0))
        return fail(error, QStringLiteral("APRS message body is empty, too long, or contains a control/reserved character"));
    decoded.body = text;
    *message = decoded;
    return true;
}
}
