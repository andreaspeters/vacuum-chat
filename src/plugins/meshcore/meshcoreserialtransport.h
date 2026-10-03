#ifndef MESHCORESERIALTRANSPORT_H
#define MESHCORESERIALTRANSPORT_H

#include "meshcorecodec.h"
#include "meshcoretransport.h"

#include <QSerialPort>

class MeshCoreSerialTransport : public MeshCoreTransport
{
    Q_OBJECT

public:
    explicit MeshCoreSerialTransport(QObject *parent = nullptr);
    ~MeshCoreSerialTransport() override;

    bool open(const QString &endpoint) override;
    void close() override;
    bool isConnected() const override;
    bool sendPayload(const QByteArray &payload) override;

private slots:
    void onReadyRead();
    void onSerialError(QSerialPort::SerialPortError error);

private:
    QSerialPort m_serialPort;
    MeshCoreFrameParser m_frameParser;
    bool m_isConnected = false;
};

#endif // MESHCORESERIALTRANSPORT_H
