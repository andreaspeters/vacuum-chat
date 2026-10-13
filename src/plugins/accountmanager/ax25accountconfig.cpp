#include "ax25accountconfig.h"

#include <QRegularExpression>

namespace Ax25AccountConfig
{
bool isValidCallsign(const QString &callsign)
{
    if (callsign != callsign.trimmed())
        return false;

    static const QRegularExpression expression(
        QStringLiteral("^[A-Z0-9]{1,6}(?:-(?:[0-9]|1[0-5]))?$"),
        QRegularExpression::CaseInsensitiveOption);
    return expression.match(callsign).hasMatch();
}

bool isValidSerialSettings(const QString &callsign, const QString &port, int baudRate)
{
    if (!isValidCallsign(callsign) || port.trimmed().isEmpty())
        return false;

    static const int supportedBaudRates[] = {
        300, 600, 1200, 2400, 4800, 9600, 14400, 19200,
        38400, 57600, 115200, 230400, 460800, 921600
    };
    for (const int supportedBaudRate : supportedBaudRates)
        if (supportedBaudRate == baudRate)
            return true;
    return false;
}

bool isValidFlexPacketSettings(const QString &callsign, int channel)
{
    // New FlexPacket AXCP accounts use monitor/UI channel 0. Keep accepting
    // previously stored channels so existing accounts remain valid while the
    // account editor migrates them to 0.
    return isValidCallsign(callsign) && channel >= 0 && channel <= 15;
}
}
