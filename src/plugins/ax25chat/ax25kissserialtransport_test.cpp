#include "ax25kissserialtransport.h"

#include <QCoreApplication>

#include <cstdio>

namespace
{
bool check(bool condition, const char *message)
{
    if (!condition)
        std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    Ax25KissSerialTransport transport;
    bool reportedError = false;
    QObject::connect(&transport, &Ax25KissSerialTransport::transportError,
                     [&reportedError](const QString &) { reportedError = true; });

    if (!check(!transport.isConnected(), "transport starts disconnected"))
        return 1;
    if (!check(!transport.open(QString(), QStringLiteral("DL1AAA"), 115200),
               "empty serial endpoint is rejected without hardware"))
        return 1;
    if (!check(reportedError, "invalid open reports a transport error"))
        return 1;

    int receivedMessages = 0;
    transport.setReceiveHandler([&receivedMessages](const QString &, const QByteArray &) {
        ++receivedMessages;
    });
    if (!check(!transport.send(QStringLiteral("W1AW"), QByteArray("hello")),
               "sending while disconnected is rejected"))
        return 1;
    if (!check(reportedError, "disconnected send reports a transport error"))
        return 1;
    transport.close();
    return check(!transport.isConnected() && receivedMessages == 0,
                 "close is safe while disconnected and no packet is fabricated")
               ? 0
               : 1;
}
