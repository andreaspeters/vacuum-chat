#ifndef AX25ACCOUNTCONFIG_H
#define AX25ACCOUNTCONFIG_H

class QString;

namespace Ax25AccountConfig
{
bool isValidCallsign(const QString &callsign);
bool isValidSerialSettings(const QString &callsign, const QString &port, int baudRate);
bool isValidFlexPacketSettings(const QString &callsign, int channel);
}

#endif // AX25ACCOUNTCONFIG_H
