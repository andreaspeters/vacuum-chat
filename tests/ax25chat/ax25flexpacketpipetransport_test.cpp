#include "ax25flexpacketpipetransport.h"
#include "aprsmessagecodec.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>

#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
#if defined(O_CLOEXEC)
constexpr int kOpenCloseOnExec = O_CLOEXEC;
#elif defined(O_NOINHERIT)
constexpr int kOpenCloseOnExec = O_NOINHERIT;
#else
#error "AX.25 pipe test requires a close-on-exec/inherit flag"
#endif

bool check(bool condition, const char *message)
{
    if (!condition)
        std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

QByteArray readAvailable(QCoreApplication &application, int fd, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        char data[1024];
        const ssize_t length = ::read(fd, data, sizeof(data));
        if (length > 0)
            return QByteArray(data, static_cast<int>(length));
        application.processEvents();
        QThread::msleep(1);
    }
    return QByteArray();
}

bool waitForMessage(QCoreApplication &application, int timeoutMs,
                    const QString &expectedSource, const QByteArray &expectedPayload,
                    QString *actualSource, QByteArray *actualPayload)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        application.processEvents();
        if (!actualPayload->isEmpty())
            break;
        QThread::msleep(1);
    }
    return *actualSource == expectedSource && *actualPayload == expectedPayload;
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir directory;
    if (!check(directory.isValid(), "temporary FIFO directory is available"))
        return 1;

    const QByteArray inputPath = QFile::encodeName(directory.filePath(QStringLiteral("flexpacketreadpipe")));
    const QByteArray outputPath = QFile::encodeName(directory.filePath(QStringLiteral("flexpacketwritepipe")));
    if (!check(::mkfifo(inputPath.constData(), 0600) == 0 &&
               ::mkfifo(outputPath.constData(), 0600) == 0,
               "synthetic FlexPacket FIFOs are created"))
        return 1;

    // Emulate FlexPacket's reader of readpipe before the plugin connects.
    const int flexpacketInput = ::open(inputPath.constData(), O_RDONLY | O_NONBLOCK | kOpenCloseOnExec);
    if (!check(flexpacketInput >= 0, "synthetic FlexPacket input endpoint opens"))
        return 1;

    Ax25FlexPacketPipeTransport transport(directory.path());
    bool reportedError = false;
    QObject::connect(&transport, &Ax25FlexPacketPipeTransport::transportError,
                     [&reportedError](const QString &) { reportedError = true; });
    if (!check(!transport.open(QStringLiteral("DL1AAA"), 1) && reportedError,
               "FlexPacket connected-data channels are rejected for APRS UI mode"))
        return 1;
    if (!check(transport.open(QStringLiteral("DL1AAA"), 0),
               "transport opens the two FlexPacket FIFO endpoints on monitor channel zero"))
        return 1;

    // Emulate FlexPacket's writer of received channel output.
    const int flexpacketOutput = ::open(outputPath.constData(), O_WRONLY | O_NONBLOCK | kOpenCloseOnExec);
    if (!check(flexpacketOutput >= 0, "synthetic FlexPacket output endpoint opens"))
        return 1;

    AprsMessage::Message outgoing;
    outgoing.destination = QStringLiteral("W1AW");
    outgoing.body = QStringLiteral("message with trailing spaces  ");
    outgoing.messageId = QStringLiteral("A31F");
    QByteArray payload;
    if (!check(AprsMessage::Codec::encode(outgoing, &payload),
               "APRS message is encoded before entering the FlexPacket pipe"))
        return 1;
    if (!check(transport.send(QStringLiteral("APRS"), payload),
               "send queues an APRS UI payload on the channel-zero pipe"))
        return 1;
    const QByteArray dataRecord = readAvailable(application, flexpacketInput, 500);
    if (!check(dataRecord == QByteArray("0|0|") + payload.toBase64() &&
               QByteArray::fromBase64(dataRecord.mid(4)) == payload,
               "pipe carries the exact APRS information field without an AXCP wrapper"))
        return 1;

    AprsMessage::Message incoming;
    incoming.destination = QStringLiteral("DL1AAA");
    incoming.body = QStringLiteral("incoming APRS text  ");
    incoming.messageId = QStringLiteral("B7");
    QByteArray incomingPayload;
    if (!check(AprsMessage::Codec::encode(incoming, &incomingPayload),
               "synthetic incoming APRS message encodes"))
        return 1;

    QString receivedSource;
    QByteArray receivedPayload;
    transport.setReceiveHandler([&](const QString &source, const QByteArray &packet) {
        receivedSource = source;
        receivedPayload = packet;
    });
    const QByteArray otherLinkDestination = QByteArray("fm W1AW to CQ ctl UI pid F0\r\n") +
        incomingPayload + "\r\r\n";
    const QByteArray otherLinkDestinationRecord = QByteArray("0|") + otherLinkDestination.toBase64();
    if (!check(::write(flexpacketOutput, otherLinkDestinationRecord.constData(),
                       static_cast<size_t>(otherLinkDestinationRecord.size())) == otherLinkDestinationRecord.size(),
               "synthetic UI frame with a non-local AX.25 destination is written"))
        return 1;
    if (!check(waitForMessage(application, 500, QStringLiteral("W1AW"), incomingPayload,
                              &receivedSource, &receivedPayload),
               "channel-zero APRS UI payload is delivered regardless of link destination"))
        return 1;

    receivedSource.clear();
    receivedPayload.clear();
    const QByteArray monitorHeader = QByteArray("fm K1ABC to DL1AAA ctl UI pid F0\r\n");
    const QByteArray monitorBody = incomingPayload + "\r\r\n";
    const QByteArray headerRecord = QByteArray("0|") + monitorHeader.toBase64();
    const QByteArray bodyRecord = QByteArray("0|") + monitorBody.toBase64();
    if (!check(::write(flexpacketOutput, headerRecord.constData(),
                       static_cast<size_t>(headerRecord.size())) == headerRecord.size() &&
               ::write(flexpacketOutput, bodyRecord.constData(),
                       static_cast<size_t>(bodyRecord.size())) == bodyRecord.size(),
               "synthetic monitor header and data arrive in separate pipe records"))
        return 1;
    if (!check(waitForMessage(application, 500, QStringLiteral("K1ABC"), incomingPayload,
                              &receivedSource, &receivedPayload),
               "split monitor records preserve the APRS payload and remove only FlexPacket's CR"))
        return 1;
    AprsMessage::Message decoded;
    if (!check(AprsMessage::Codec::decode(receivedPayload, &decoded) &&
               decoded.destination == incoming.destination && decoded.body == incoming.body &&
               decoded.messageId == incoming.messageId,
               "received FlexPacket payload decodes to the original APRS message"))
        return 1;

    transport.close();
    ::close(flexpacketInput);
    ::close(flexpacketOutput);
    return 0;
}
