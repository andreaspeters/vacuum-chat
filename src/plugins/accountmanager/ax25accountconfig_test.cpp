#include "ax25accountconfig.h"

#include <QString>
#include <QStringList>
#include <iostream>

int main()
{
    const QStringList validCallsigns = {
        QStringLiteral("DL1AAA"), QStringLiteral("dl1aaa-7"),
        QStringLiteral("N0CALL-15"), QStringLiteral("A-0")
    };
    for (const QString &callsign : validCallsigns) {
        if (!Ax25AccountConfig::isValidCallsign(callsign)) {
            std::cerr << "valid AX.25 callsign rejected: " << callsign.toStdString() << '\n';
            return 1;
        }
    }

    const QStringList invalidCallsigns = {
        QString(), QStringLiteral("TOOLONG1"), QStringLiteral("DL1AAA-16"),
        QStringLiteral("DL1AAA-"), QStringLiteral("DL1 AAA"), QStringLiteral("CALL/SSID")
    };
    for (const QString &callsign : invalidCallsigns) {
        if (Ax25AccountConfig::isValidCallsign(callsign)) {
            std::cerr << "invalid AX.25 callsign accepted: " << callsign.toStdString() << '\n';
            return 1;
        }
    }

    if (!Ax25AccountConfig::isValidSerialSettings(QStringLiteral("DL1AAA-7"),
            QStringLiteral("COM3"), 115200) ||
        Ax25AccountConfig::isValidSerialSettings(QStringLiteral("DL1AAA-7"), QString(), 115200) ||
        Ax25AccountConfig::isValidSerialSettings(QStringLiteral("DL1AAA-7"),
            QStringLiteral("COM3"), 12345)) {
        std::cerr << "AX.25 serial account settings validation failed\n";
        return 1;
    }
    return 0;
}
