#include "ax25kissserialtransport.h"

#include <utility>

Ax25KissSerialTransport::Ax25KissSerialTransport(QObject *parent)
    : QObject(parent)
{
    connect(&m_serialPort, &QSerialPort::readyRead,
            this, &Ax25KissSerialTransport::onReadyRead);
    connect(&m_serialPort, &QSerialPort::errorOccurred,
            this, &Ax25KissSerialTransport::onSerialError);
}

Ax25KissSerialTransport::~Ax25KissSerialTransport()
{
    close();
}

bool Ax25KissSerialTransport::open(const QString &portName,
                                   const QString &localCallsign,
                                   int baudRate)
{
    if (m_isConnected || m_serialPort.isOpen()) {
        emit transportError(tr("Serial transport is already open"));
        return false;
    }
    if (portName.trimmed().isEmpty()) {
        emit transportError(tr("Serial port name is empty"));
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

    m_frameDecoder.reset();
    m_serialPort.setPortName(portName.trimmed());
    m_serialPort.setDataBits(QSerialPort::Data8);
    m_serialPort.setParity(QSerialPort::NoParity);
    m_serialPort.setStopBits(QSerialPort::OneStop);
    m_serialPort.setFlowControl(QSerialPort::NoFlowControl);
    if (!m_serialPort.setBaudRate(baudRate)) {
        emit transportError(tr("Unsupported serial baud rate %1").arg(baudRate));
        return false;
    }

    if (!m_serialPort.open(QIODevice::ReadWrite)) {
        emit transportError(tr("Failed to open serial port %1: %2")
                            .arg(portName, m_serialPort.errorString()));
        return false;
    }

    m_localCallsign = localCallsign.toUpper();
    m_isConnected = true;
    emit connected();
    return true;
}

void Ax25KissSerialTransport::close()
{
    const bool wasConnected = m_isConnected || m_serialPort.isOpen();
    m_isConnected = false;
    m_localCallsign.clear();
    if (m_serialPort.isOpen())
        m_serialPort.close();
    m_frameDecoder.reset();
    if (wasConnected)
        emit disconnected();
}

bool Ax25KissSerialTransport::isConnected() const
{
    return m_isConnected && m_serialPort.isOpen();
}

bool Ax25KissSerialTransport::send(const QString &destination, const QByteArray &payload)
{
    if (!isConnected()) {
        emit transportError(tr("Cannot send AX.25 data: serial port is not connected"));
        return false;
    }

    QByteArray ax25Frame;
    QString error;
    if (!Ax25Kiss::Ax25UiFrameCodec::encodeUiFrame(
            m_localCallsign, destination, payload, &ax25Frame, &error)) {
        emit transportError(error);
        return false;
    }
    const QByteArray kissFrame = Ax25Kiss::KissCodec::encodeDataFrame(ax25Frame);
    if (kissFrame.isEmpty()) {
        emit transportError(tr("Failed to encode KISS data frame"));
        return false;
    }

    if (m_serialPort.write(kissFrame) != kissFrame.size()) {
        emit transportError(tr("Failed to queue serial frame: %1")
                            .arg(m_serialPort.errorString()));
        close();
        return false;
    }
    return true;
}

void Ax25KissSerialTransport::setReceiveHandler(ReceiveHandler handler)
{
    m_receiveHandler = std::move(handler);
}

void Ax25KissSerialTransport::onReadyRead()
{
    if (!isConnected())
        return;

    const QList<QByteArray> packets = m_frameDecoder.feed(m_serialPort.readAll());
    for (const QByteArray &packet : packets) {
        Ax25Kiss::Ax25UiFrame frame;
        if (!Ax25Kiss::Ax25UiFrameCodec::decodeUiFrame(packet, &frame))
            continue;
        if (m_receiveHandler)
            m_receiveHandler(frame.source, frame.information);
    }
}

void Ax25KissSerialTransport::onSerialError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError || !m_isConnected)
        return;

    emit transportError(tr("Serial port error: %1").arg(m_serialPort.errorString()));
    if (error == QSerialPort::ResourceError ||
        error == QSerialPort::DeviceNotFoundError || !m_serialPort.isOpen())
        close();
}
