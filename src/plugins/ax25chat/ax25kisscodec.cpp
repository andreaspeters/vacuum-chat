#include "ax25kisscodec.h"

#include <QtGlobal>

namespace
{
bool parseCallsign(const QString &input, QString *base, quint8 *ssid, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    if (input.isEmpty() || input != input.trimmed())
        return fail(QStringLiteral("Callsign is empty or contains surrounding whitespace"));

    for (const QChar character : input) {
        if (character.unicode() > 0x7f)
            return fail(QStringLiteral("Callsign must contain ASCII characters only"));
    }

    const QString value = input.toUpper();
    QString call = value;
    int stationId = 0;
    const int dash = value.indexOf(QLatin1Char('-'));
    if (dash >= 0) {
        if (dash == 0 || value.indexOf(QLatin1Char('-'), dash + 1) >= 0)
            return fail(QStringLiteral("Callsign has an invalid SSID suffix"));
        call = value.left(dash);
        const QString suffix = value.mid(dash + 1);
        if (suffix.isEmpty() || suffix.size() > 2 ||
            (suffix.size() == 2 && suffix.at(0) != QLatin1Char('1')))
            return fail(QStringLiteral("SSID must be between 0 and 15"));
        for (const QChar character : suffix) {
            if (character < QLatin1Char('0') || character > QLatin1Char('9'))
                return fail(QStringLiteral("SSID must contain decimal digits"));
            stationId = stationId * 10 + character.digitValue();
        }
        if (stationId > 15)
            return fail(QStringLiteral("SSID must be between 0 and 15"));
    }

    if (call.isEmpty() || call.size() > 6 ||
        call.at(0) < QLatin1Char('A') || call.at(0) > QLatin1Char('Z'))
        return fail(QStringLiteral("Callsign base must start with a letter and be at most six characters"));
    for (const QChar character : call) {
        const bool letter = character >= QLatin1Char('A') && character <= QLatin1Char('Z');
        const bool digit = character >= QLatin1Char('0') && character <= QLatin1Char('9');
        if (!letter && !digit)
            return fail(QStringLiteral("Callsign base may contain only letters and digits"));
    }

    if (base)
        *base = call;
    if (ssid)
        *ssid = static_cast<quint8>(stationId);
    return true;
}

bool encodeAddress(const QString &callsign, bool destination, bool lastAddress,
                   QByteArray *output, QString *error)
{
    QString base;
    quint8 ssid = 0;
    if (!parseCallsign(callsign, &base, &ssid, error))
        return false;

    for (int i = 0; i < 6; ++i) {
        const uchar character = i < base.size()
            ? static_cast<uchar>(base.at(i).toLatin1())
            : static_cast<uchar>(' ');
        output->append(static_cast<char>(character << 1));
    }

    // AX.25 v2 command: destination C=1, source C=0. Reserved bits are 1.
    quint8 ssidField = static_cast<quint8>(0x60 | (ssid << 1));
    if (destination)
        ssidField = static_cast<quint8>(ssidField | 0x80);
    if (lastAddress)
        ssidField = static_cast<quint8>(ssidField | 0x01);
    output->append(static_cast<char>(ssidField));
    return true;
}

bool decodeAddress(const QByteArray &frame, int offset, bool mustBeLast,
                   QString *callsign, quint8 *ssid, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (offset < 0 || frame.size() < offset + 7)
        return fail(QStringLiteral("AX.25 address field is truncated"));

    QString base;
    bool paddingStarted = false;
    for (int i = 0; i < 6; ++i) {
        const quint8 shifted = static_cast<quint8>(static_cast<unsigned char>(frame.at(offset + i)));
        if (shifted & 0x01)
            return fail(QStringLiteral("AX.25 shifted callsign has a nonzero low bit"));
        const char character = static_cast<char>(shifted >> 1);
        if (character == ' ') {
            paddingStarted = true;
            continue;
        }
        if (paddingStarted || !((character >= 'A' && character <= 'Z') ||
                                (character >= '0' && character <= '9')))
            return fail(QStringLiteral("AX.25 callsign is malformed"));
        base.append(QLatin1Char(character));
    }
    if (base.isEmpty() || base.at(0) < QLatin1Char('A') || base.at(0) > QLatin1Char('Z'))
        return fail(QStringLiteral("AX.25 callsign base must start with a letter"));

    const quint8 ssidField = static_cast<quint8>(static_cast<unsigned char>(frame.at(offset + 6)));
    if ((ssidField & 0x60) != 0x60)
        return fail(QStringLiteral("AX.25 reserved SSID bits are not set"));
    const bool isLast = (ssidField & 0x01) != 0;
    if (isLast != mustBeLast)
        return fail(QStringLiteral("AX.25 direct frame contains an unsupported repeater path"));

    const quint8 decodedSsid = static_cast<quint8>((ssidField >> 1) & 0x0f);
    if (callsign) {
        *callsign = base;
        if (decodedSsid != 0)
            *callsign += QLatin1Char('-') + QString::number(decodedSsid);
    }
    if (ssid)
        *ssid = decodedSsid;
    return true;
}
}

namespace Ax25Kiss
{
QByteArray KissCodec::encodeDataFrame(const QByteArray &payload, quint8 port)
{
    if (port > 0x0f)
        return QByteArray();

    QByteArray frame;
    frame.reserve(payload.size() + 4);
    frame.append(static_cast<char>(FEND));
    frame.append(static_cast<char>((port << 4) | DataCommand));
    for (const char byte : payload) {
        const quint8 value = static_cast<quint8>(static_cast<unsigned char>(byte));
        if (value == FEND) {
            frame.append(static_cast<char>(FESC));
            frame.append(static_cast<char>(TFEND));
        } else if (value == FESC) {
            frame.append(static_cast<char>(FESC));
            frame.append(static_cast<char>(TFESC));
        } else {
            frame.append(byte);
        }
    }
    frame.append(static_cast<char>(FEND));
    return frame;
}

KissStreamDecoder::KissStreamDecoder(int maximumPayloadBytes, quint8 port)
    : m_maximumPayloadBytes(qBound(0, maximumPayloadBytes, 1024 * 1024)),
      m_port(port <= 0x0f ? port : 0),
      m_inFrame(false),
      m_escapePending(false),
      m_discardFrame(false)
{
}

QList<QByteArray> KissStreamDecoder::feed(const QByteArray &data)
{
    QList<QByteArray> packets;
    for (const char rawByte : data) {
        const quint8 byte = static_cast<quint8>(static_cast<unsigned char>(rawByte));
        if (byte == KissCodec::FEND) {
            if (m_inFrame && !m_discardFrame && !m_escapePending && !m_buffer.isEmpty()) {
                const quint8 type = static_cast<quint8>(static_cast<unsigned char>(m_buffer.at(0)));
                if ((type & 0x0f) == KissCodec::DataCommand && (type >> 4) == m_port)
                    packets.append(m_buffer.mid(1));
            }
            m_inFrame = true;
            m_escapePending = false;
            m_discardFrame = false;
            m_buffer.clear();
            continue;
        }

        if (!m_inFrame || m_discardFrame)
            continue;

        quint8 decodedByte = byte;
        if (m_escapePending) {
            m_escapePending = false;
            if (byte == KissCodec::TFEND)
                decodedByte = KissCodec::FEND;
            else if (byte == KissCodec::TFESC)
                decodedByte = KissCodec::FESC;
            else {
                m_discardFrame = true;
                m_buffer.clear();
                continue;
            }
        } else if (byte == KissCodec::FESC) {
            m_escapePending = true;
            continue;
        }

        // The buffer contains one KISS type byte plus at most the configured
        // number of decoded packet bytes.
        if (m_buffer.size() >= m_maximumPayloadBytes + 1) {
            m_discardFrame = true;
            m_buffer.clear();
            continue;
        }
        m_buffer.append(static_cast<char>(decodedByte));
    }
    return packets;
}

void KissStreamDecoder::reset()
{
    m_buffer.clear();
    m_inFrame = false;
    m_escapePending = false;
    m_discardFrame = false;
}

bool Ax25UiFrameCodec::encodeUiFrame(const QString &sourceCallsign,
                                     const QString &destinationCallsign,
                                     const QByteArray &information,
                                     QByteArray *frame,
                                     QString *error)
{
    const auto fail = [frame, error](const QString &message) {
        if (frame)
            frame->clear();
        if (error)
            *error = message;
        return false;
    };
    if (!frame)
        return fail(QStringLiteral("AX.25 frame output is null"));
    frame->clear();
    if (information.size() > 256)
        return fail(QStringLiteral("AX.25 UI information field exceeds 256 bytes"));

    QByteArray encoded;
    encoded.reserve(16 + information.size());
    if (!encodeAddress(destinationCallsign, true, false, &encoded, error) ||
        !encodeAddress(sourceCallsign, false, true, &encoded, error)) {
        if (error && error->isEmpty())
            *error = QStringLiteral("AX.25 address is invalid");
        return false;
    }
    encoded.append(static_cast<char>(0x03)); // UI control, Poll/Final clear
    encoded.append(static_cast<char>(0xf0)); // No layer-3 protocol
    encoded.append(information);
    *frame = encoded;
    if (error)
        error->clear();
    return true;
}

bool Ax25UiFrameCodec::decodeUiFrame(const QByteArray &frame,
                                     Ax25UiFrame *decoded,
                                     QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (!decoded)
        return fail(QStringLiteral("AX.25 decoded-frame output is null"));
    if (frame.size() < 16 || frame.size() > 16 + 256)
        return fail(QStringLiteral("AX.25 UI frame has an invalid length"));

    Ax25UiFrame result;
    if (!decodeAddress(frame, 0, false, &result.destination, nullptr, error) ||
        !decodeAddress(frame, 7, true, &result.source, nullptr, error))
        return false;
    const quint8 control = static_cast<quint8>(static_cast<unsigned char>(frame.at(14)));
    if ((control & 0xef) != 0x03)
        return fail(QStringLiteral("AX.25 frame is not a UI frame"));
    if (static_cast<quint8>(static_cast<unsigned char>(frame.at(15))) != 0xf0)
        return fail(QStringLiteral("AX.25 frame has an unsupported PID"));

    result.information = frame.mid(16);
    *decoded = result;
    if (error)
        error->clear();
    return true;
}
}
