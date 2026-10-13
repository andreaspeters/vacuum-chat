#include "ax25kissserialtransport.h"
#include "aprsmessagecodec.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>

namespace
{
bool check(bool condition, const char *message)
{
    if (!condition)
        std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

bool waitForUiFrame(QCoreApplication &application, int masterFd, int timeoutMs,
                    Ax25Kiss::Ax25UiFrame *uiFrame)
{
    Ax25Kiss::KissStreamDecoder decoder;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        application.processEvents();
        char bytes[512];
        const ssize_t count = ::read(masterFd, bytes, sizeof(bytes));
        if (count > 0) {
            const QList<QByteArray> packets = decoder.feed(QByteArray(bytes, static_cast<int>(count)));
            for (const QByteArray &packet : packets) {
                if (Ax25Kiss::Ax25UiFrameCodec::decodeUiFrame(packet, uiFrame))
                    return true;
            }
        } else if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            return false;
        }
        QThread::msleep(1);
    }
    return false;
}

bool waitForPayload(QCoreApplication &application, const QString &expectedSource,
                    const QByteArray &expectedPayload, const QString &actualSource,
                    const QByteArray &actualPayload, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs && actualPayload.isEmpty()) {
        application.processEvents();
        QThread::msleep(1);
    }
    return actualSource == expectedSource && actualPayload == expectedPayload;
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
    if (!check(!transport.isConnected() && receivedMessages == 0,
               "close is safe while disconnected and no packet is fabricated"))
        return 1;

    const int masterFd = ::posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (!check(masterFd >= 0 && ::grantpt(masterFd) == 0 && ::unlockpt(masterFd) == 0,
               "synthetic serial PTY is created"))
        return 1;
    char slavePath[256] = {};
    if (!check(::ptsname_r(masterFd, slavePath, sizeof(slavePath)) == 0,
               "synthetic serial slave path is available")) {
        ::close(masterFd);
        return 1;
    }
    if (!check(transport.open(QString::fromLocal8Bit(slavePath), QStringLiteral("N0CALL"), 115200),
               "TTY transport opens the synthetic KISS serial port")) {
        ::close(masterFd);
        return 1;
    }

    AprsMessage::Message outgoing;
    outgoing.destination = QStringLiteral("K1ABC");
    outgoing.body = QStringLiteral("hello APRS");
    outgoing.messageId = QStringLiteral("C2");
    QByteArray outgoingWire;
    if (!check(AprsMessage::Codec::encode(outgoing, &outgoingWire) &&
               transport.send(QStringLiteral("APRS"), outgoingWire),
               "TTY queues a standard APRS message as an AX.25 UI payload")) {
        transport.close();
        ::close(masterFd);
        return 1;
    }
    Ax25Kiss::Ax25UiFrame sentFrame;
    if (!check(waitForUiFrame(application, masterFd, 500, &sentFrame) &&
               sentFrame.source == QStringLiteral("N0CALL") &&
               sentFrame.destination == QStringLiteral("APRS") &&
               sentFrame.information == outgoingWire,
               "KISS serial TX retains APRS destination and exact information field")) {
        transport.close();
        ::close(masterFd);
        return 1;
    }

    AprsMessage::Message incoming;
    incoming.destination = QStringLiteral("N0CALL");
    incoming.body = QStringLiteral("reply from APRS");
    incoming.messageId = QStringLiteral("D3");
    QByteArray incomingWire;
    QByteArray ax25Frame;
    QString error;
    if (!check(AprsMessage::Codec::encode(incoming, &incomingWire, &error) &&
               Ax25Kiss::Ax25UiFrameCodec::encodeUiFrame(
                   QStringLiteral("K1ABC"), QStringLiteral("APRS"), incomingWire,
                   &ax25Frame, &error),
               "synthetic incoming APRS message encodes as an AX.25 UI frame")) {
        transport.close();
        ::close(masterFd);
        return 1;
    }
    QString receivedSource;
    QByteArray receivedPayload;
    transport.setReceiveHandler([&receivedSource, &receivedPayload](const QString &source,
                                                                    const QByteArray &payload) {
        receivedSource = source;
        receivedPayload = payload;
    });
    const QByteArray kissFrame = Ax25Kiss::KissCodec::encodeDataFrame(ax25Frame);
    const ssize_t written = ::write(masterFd, kissFrame.constData(),
                                    static_cast<size_t>(kissFrame.size()));
    if (!check(written == kissFrame.size(), "synthetic APRS KISS frame is injected into the TTY")) {
        transport.close();
        ::close(masterFd);
        return 1;
    }
    const bool received = waitForPayload(application, QStringLiteral("K1ABC"), incomingWire,
                                         receivedSource, receivedPayload, 500);
    transport.close();
    ::close(masterFd);
    return check(received,
                 "TTY RX delivers APRS information field unchanged with AX.25 source") ? 0 : 1;
}
