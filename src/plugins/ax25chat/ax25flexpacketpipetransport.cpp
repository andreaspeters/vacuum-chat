#include "ax25flexpacketpipetransport.h"

#include "ax25kisscodec.h"

#include <QDir>
#include <QDebug>
#include <QFile>
#include <QRegularExpression>
#include <QSocketNotifier>
#include <QTimer>

#include <cerrno>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>

namespace
{
#if defined(O_CLOEXEC)
constexpr int kOpenCloseOnExec = O_CLOEXEC;
#elif defined(O_NOINHERIT)
constexpr int kOpenCloseOnExec = O_NOINHERIT;
#else
#error "AX.25 pipe transport requires a close-on-exec/inherit flag"
#endif

QString canonicalAddress(const QString &callsign)
{
    QByteArray frame;
    Ax25Kiss::Ax25UiFrame decoded;
    if (!Ax25Kiss::Ax25UiFrameCodec::encodeUiFrame(
            QStringLiteral("N0CALL"), callsign, QByteArray(), &frame) ||
        !Ax25Kiss::Ax25UiFrameCodec::decodeUiFrame(frame, &decoded))
        return QString();
    return decoded.destination;
}
}

Ax25FlexPacketPipeTransport::Ax25FlexPacketPipeTransport(QObject *parent)
    : Ax25FlexPacketPipeTransport(QStringLiteral("/tmp"), parent)
{
}

Ax25FlexPacketPipeTransport::Ax25FlexPacketPipeTransport(const QString &pipeDirectory,
                                                         QObject *parent)
    : QObject(parent),
      m_pipeDirectory(QDir(pipeDirectory).absolutePath()),
      m_inputTimer(new QTimer(this)),
      m_outputTimer(new QTimer(this))
{
    m_inputTimer->setSingleShot(true);
    m_inputTimer->setInterval(20);
    connect(m_inputTimer, &QTimer::timeout,
            this, &Ax25FlexPacketPipeTransport::flushPipeInput);
    m_outputTimer->setSingleShot(true);
    connect(m_outputTimer, &QTimer::timeout,
            this, &Ax25FlexPacketPipeTransport::writeNextRecord);
}

Ax25FlexPacketPipeTransport::~Ax25FlexPacketPipeTransport()
{
    close();
}

bool Ax25FlexPacketPipeTransport::open(const QString &localCallsign, int channel)
{
    if (isConnected()) {
        emit transportError(tr("FlexPacket pipe transport is already open"));
        return false;
    }
    if (channel != 0) {
        emit transportError(tr("APRS over FlexPacket requires monitor channel 0 for unconnected UI frames"));
        return false;
    }

    QByteArray validationFrame;
    QString validationError;
    if (!Ax25Kiss::Ax25UiFrameCodec::encodeUiFrame(
            localCallsign, QStringLiteral("N0CALL"), QByteArray(),
            &validationFrame, &validationError)) {
        emit transportError(validationError);
        return false;
    }

    const QByteArray inputPath = QFile::encodeName(
        QDir(m_pipeDirectory).filePath(QStringLiteral("flexpacketreadpipe")));
    const QByteArray outputPath = QFile::encodeName(
        QDir(m_pipeDirectory).filePath(QStringLiteral("flexpacketwritepipe")));
    struct stat readPipeInfo {};
    struct stat writePipeInfo {};
    if (::stat(inputPath.constData(), &readPipeInfo) != 0 || !S_ISFIFO(readPipeInfo.st_mode) ||
        ::stat(outputPath.constData(), &writePipeInfo) != 0 || !S_ISFIFO(writePipeInfo.st_mode)) {
        emit transportError(tr("FlexPacket FIFOs were not found; enable External Mode in FlexPacket first"));
        return false;
    }

    // FlexPacket reads its input from flexpacketreadpipe and writes received
    // channel data to flexpacketwritepipe. O_RDWR avoids an EOF busy loop while
    // FlexPacket is between writes; the separate write end must have a reader.
    m_readFd = ::open(outputPath.constData(), O_RDWR | O_NONBLOCK | kOpenCloseOnExec);
    if (m_readFd < 0) {
        emit transportError(tr("Cannot open FlexPacket output pipe: %1")
                                .arg(QString::fromLocal8Bit(std::strerror(errno))));
        close();
        return false;
    }
    const int writeProbeFd = ::open(inputPath.constData(), O_WRONLY | O_NONBLOCK | kOpenCloseOnExec);
    if (writeProbeFd < 0) {
        emit transportError(tr("Cannot open FlexPacket input pipe: %1")
                                .arg(QString::fromLocal8Bit(std::strerror(errno))));
        close();
        return false;
    }
    ::close(writeProbeFd);

    m_channel = channel;
    m_localCallsign = canonicalAddress(localCallsign);
    qInfo().noquote() << "[ax25-flexpacket-pipe] open channel=" << m_channel;
    m_pipeBuffer.clear();
    m_monitorBuffer.clear();
    m_monitorSource.clear();
    m_readNotifier = new QSocketNotifier(m_readFd, QSocketNotifier::Read, this);
    connect(m_readNotifier, &QSocketNotifier::activated,
            this, &Ax25FlexPacketPipeTransport::onPipeReadable);
    emit connected();
    return true;
}

void Ax25FlexPacketPipeTransport::close()
{
    const bool wasConnected = isConnected();
    m_inputTimer->stop();
    m_outputTimer->stop();
    m_pendingRecords.clear();
    if (m_readNotifier) {
        m_readNotifier->setEnabled(false);
        delete m_readNotifier;
        m_readNotifier = nullptr;
    }
    if (m_readFd >= 0) {
        ::close(m_readFd);
        m_readFd = -1;
    }
    if (m_writeFd >= 0) {
        ::close(m_writeFd);
        m_writeFd = -1;
    }
    m_channel = -1;
    m_localCallsign.clear();
    m_pipeBuffer.clear();
    m_monitorBuffer.clear();
    m_monitorSource.clear();
    if (wasConnected)
        emit disconnected();
}

bool Ax25FlexPacketPipeTransport::isConnected() const
{
    return m_readFd >= 0;
}

bool Ax25FlexPacketPipeTransport::send(const QString &destination, const QByteArray &payload)
{
    if (!isConnected()) {
        emit transportError(tr("Cannot send AX.25 data: FlexPacket pipes are not connected"));
        return false;
    }
    if (payload.isEmpty()) {
        emit transportError(tr("Cannot send an empty APRS payload through FlexPacket"));
        return false;
    }

    QByteArray validationFrame;
    QString validationError;
    if (!Ax25Kiss::Ax25UiFrameCodec::encodeUiFrame(
            m_localCallsign, destination, QByteArray(), &validationFrame, &validationError)) {
        emit transportError(validationError);
        return false;
    }
    // Channel 0 sends connectionless UI frames. The logical recipient is part
    // of the APRS message; do not change TFKISS's shared unproto destination.
    return writeRecord(payload);
}

void Ax25FlexPacketPipeTransport::setReceiveHandler(ReceiveHandler handler)
{
    m_receiveHandler = std::move(handler);
}

bool Ax25FlexPacketPipeTransport::writeRecord(const QByteArray &payload)
{
    qInfo().noquote() << "[ax25-flexpacket-pipe] tx channel=" << m_channel
                      << "code=" << 0
                      << "payload-bytes=" << payload.size();
    QByteArray record = QByteArray::number(m_channel);
    record += "|0|";
    record += payload.toBase64();
    if (record.size() >= PIPE_BUF) {
        emit transportError(tr("FlexPacket pipe record exceeds the atomic FIFO write limit"));
        return false;
    }

    m_pendingRecords.enqueue(record);
    if (!m_outputTimer->isActive())
        m_outputTimer->start(0);
    return true;
}

void Ax25FlexPacketPipeTransport::writeNextRecord()
{
    if (!isConnected() || m_pendingRecords.isEmpty())
        return;
    const QByteArray record = m_pendingRecords.dequeue();
    if (!writePendingRecord(record)) {
        close();
        return;
    }
    if (!m_pendingRecords.isEmpty())
        m_outputTimer->start(100);
}

bool Ax25FlexPacketPipeTransport::writePendingRecord(const QByteArray &record)
{
    const QByteArray inputPath = QFile::encodeName(
        QDir(m_pipeDirectory).filePath(QStringLiteral("flexpacketreadpipe")));
    const int writeFd = ::open(inputPath.constData(), O_WRONLY | O_NONBLOCK | kOpenCloseOnExec);
    if (writeFd < 0) {
        emit transportError(tr("Cannot open FlexPacket input pipe: %1")
                                .arg(QString::fromLocal8Bit(std::strerror(errno))));
        return false;
    }

    ssize_t written;
    do {
        written = ::write(writeFd, record.constData(), static_cast<size_t>(record.size()));
    } while (written < 0 && errno == EINTR);
    const int writeError = errno;
    ::close(writeFd);
    if (written != record.size()) {
        emit transportError(tr("Failed to write to FlexPacket pipe: %1")
                                .arg(QString::fromLocal8Bit(std::strerror(writeError))));
        return false;
    }
    return true;
}

void Ax25FlexPacketPipeTransport::onPipeReadable()
{
    char buffer[4096];
    for (;;) {
        const ssize_t bytesRead = ::read(m_readFd, buffer, sizeof(buffer));
        if (bytesRead > 0) {
            m_pipeBuffer.append(buffer, static_cast<int>(bytesRead));
            continue;
        }
        if (bytesRead < 0 && errno == EINTR)
            continue;
        if (bytesRead < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
            emit transportError(tr("Failed to read FlexPacket pipe: %1")
                                    .arg(QString::fromLocal8Bit(std::strerror(errno))));
        break;
    }
    processPipeBuffer(false);
    if (!m_pipeBuffer.isEmpty())
        m_inputTimer->start();
}

void Ax25FlexPacketPipeTransport::flushPipeInput()
{
    processPipeBuffer(true);
}

void Ax25FlexPacketPipeTransport::processPipeBuffer(bool flushLastRecord)
{
    static const QRegularExpression header(QStringLiteral("([0-9]{1,2})\\|"));
    for (;;) {
        const QRegularExpressionMatch first = header.match(QString::fromLatin1(m_pipeBuffer));
        if (!first.hasMatch()) {
            if (flushLastRecord)
                m_pipeBuffer.clear();
            else if (m_pipeBuffer.size() > 4)
                m_pipeBuffer = m_pipeBuffer.right(4);
            return;
        }
        if (first.capturedStart() > 0)
            m_pipeBuffer.remove(0, first.capturedStart());

        const int dataStart = first.capturedLength();
        const QRegularExpressionMatch next = header.match(QString::fromLatin1(m_pipeBuffer), dataStart);
        if (next.hasMatch()) {
            deliverPipeRecord(first.captured(1).toInt(), m_pipeBuffer.mid(dataStart,
                next.capturedStart() - dataStart));
            m_pipeBuffer.remove(0, next.capturedStart());
            continue;
        }
        if (flushLastRecord) {
            deliverPipeRecord(first.captured(1).toInt(), m_pipeBuffer.mid(dataStart));
            m_pipeBuffer.clear();
        }
        return;
    }
}

void Ax25FlexPacketPipeTransport::deliverPipeRecord(int channel, const QByteArray &base64Data)
{
    if (channel != 0)
        return;
    const QByteArray monitorData = QByteArray::fromBase64(base64Data);
    if (monitorData.isEmpty())
        return;
    m_monitorBuffer += monitorData;
    if (m_monitorBuffer.size() > 8192) {
        m_monitorBuffer.clear();
        m_monitorSource.clear();
    }

    static const QRegularExpression ansi(QStringLiteral("\\x1B\\[[0-9;]*m"));
    static const QRegularExpression uiHeader(
        QStringLiteral("^\\s*fm\\s+([^\\s]+)\\s+to\\s+([^\\s]+).*\\bctl\\s+UI\\b.*\\bpid\\s+F0\\b"),
        QRegularExpression::CaseInsensitiveOption);
    for (;;) {
        const int lineEnd = m_monitorBuffer.indexOf("\r\n");
        if (lineEnd < 0)
            return;
        QByteArray line = m_monitorBuffer.left(lineEnd);
        m_monitorBuffer.remove(0, lineEnd + 2);
        QString text = QString::fromUtf8(line);
        text.remove(ansi);
        if (text.isEmpty())
            continue;

        const QRegularExpressionMatch headerMatch = uiHeader.match(text.trimmed());
        if (headerMatch.hasMatch()) {
            m_monitorSource = canonicalAddress(headerMatch.captured(1));
            continue;
        }

        if (m_monitorSource.isEmpty())
            continue;
        const QString source = m_monitorSource;
        m_monitorSource.clear();
        if (source == m_localCallsign || !m_receiveHandler)
            continue;

        QByteArray payload = text.toUtf8();
        if (payload.endsWith('\r'))
            payload.chop(1); // Code-0 adds a CR after the application data.
        if (!payload.isEmpty())
            m_receiveHandler(source, payload);
    }
}
