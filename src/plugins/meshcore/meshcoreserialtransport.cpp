#include "meshcoreserialtransport.h"

#include <QSerialPort>
#include <vector>

MeshCoreSerialTransport::MeshCoreSerialTransport(QObject *parent)
    : MeshCoreTransport(parent)
{
    connect(&m_serialPort, &QSerialPort::readyRead,
            this, &MeshCoreSerialTransport::onReadyRead);
    connect(&m_serialPort, &QSerialPort::errorOccurred,
            this, &MeshCoreSerialTransport::onSerialError);
}

MeshCoreSerialTransport::~MeshCoreSerialTransport()
{
    close();
}

bool MeshCoreSerialTransport::open(const QString &endpoint)
{
    if (m_isConnected || m_serialPort.isOpen()) {
        emit transportError(tr("Serial transport is already open"));
        return false;
    }
    if (endpoint.isEmpty()) {
        emit transportError(tr("Serial port name is empty"));
        return false;
    }

    m_frameParser.reset();
    m_serialPort.setPortName(endpoint);
    m_serialPort.setBaudRate(QSerialPort::Baud115200);
    m_serialPort.setDataBits(QSerialPort::Data8);
    m_serialPort.setParity(QSerialPort::NoParity);
    m_serialPort.setStopBits(QSerialPort::OneStop);
    m_serialPort.setFlowControl(QSerialPort::NoFlowControl);

    if (!m_serialPort.open(QIODevice::ReadWrite)) {
        emit transportError(tr("Failed to open serial port %1: %2")
                            .arg(endpoint, m_serialPort.errorString()));
        return false;
    }

    m_isConnected = true;
    emit connected();
    return true;
}

void MeshCoreSerialTransport::close()
{
    const bool wasConnected = m_isConnected;
    m_isConnected = false;
    if (m_serialPort.isOpen())
        m_serialPort.close();
    m_frameParser.reset();

    if (wasConnected)
        emit disconnected();
}

bool MeshCoreSerialTransport::isConnected() const
{
    return m_isConnected && m_serialPort.isOpen();
}

bool MeshCoreSerialTransport::sendPayload(const QByteArray &payload)
{
    if (!isConnected()) {
        emit transportError(tr("Cannot send data: serial port is not connected"));
        return false;
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve(static_cast<std::size_t>(payload.size()));
    for (char byte : payload)
        bytes.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(byte)));

    const std::vector<std::uint8_t> encoded = MeshCoreCompanionCodec::encodeFrame(bytes);
    if (encoded.empty()) {
        emit transportError(tr("MeshCore serial payload is too large"));
        return false;
    }

    const QByteArray frame(reinterpret_cast<const char *>(encoded.data()),
                           static_cast<int>(encoded.size()));
    if (m_serialPort.write(frame) != frame.size()) {
        emit transportError(tr("Failed to queue serial frame: %1")
                            .arg(m_serialPort.errorString()));
        close();
        return false;
    }

    return true;
}

void MeshCoreSerialTransport::onReadyRead()
{
    if (!isConnected())
        return;

    const QByteArray data = m_serialPort.readAll();
    std::vector<std::uint8_t> bytes;
    bytes.reserve(static_cast<std::size_t>(data.size()));
    for (char byte : data)
        bytes.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(byte)));

    const std::vector<std::vector<std::uint8_t>> frames = m_frameParser.feed(bytes);
    for (const std::vector<std::uint8_t> &frame : frames) {
        const QByteArray payload(reinterpret_cast<const char *>(frame.data()),
                                 static_cast<int>(frame.size()));
        emit packetReceived(payload);
    }
}

void MeshCoreSerialTransport::onSerialError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError || !m_isConnected)
        return;

    emit transportError(tr("Serial port error: %1").arg(m_serialPort.errorString()));
    if (error == QSerialPort::ResourceError ||
        error == QSerialPort::DeviceNotFoundError || !m_serialPort.isOpen())
        close();
}
