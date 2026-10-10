#ifndef AX25KISSSERIALTRANSPORT_H
#define AX25KISSSERIALTRANSPORT_H

#include "ax25kisscodec.h"
#include "axcp_reliable_engine.h"

#include <QSerialPort>
#include <QObject>

class Ax25KissSerialTransport : public QObject, public Axcp::Transport
{
    Q_OBJECT

public:
    explicit Ax25KissSerialTransport(QObject *parent = nullptr);
    ~Ax25KissSerialTransport() override;

    bool open(const QString &portName, const QString &localCallsign, int baudRate);
    void close();
    bool isConnected() const;

    bool send(const QString &destination, const QByteArray &payload) override;
    void setReceiveHandler(ReceiveHandler handler) override;

signals:
    void connected();
    void disconnected();
    void transportError(const QString &error);

private slots:
    void onReadyRead();
    void onSerialError(QSerialPort::SerialPortError error);

private:
    QSerialPort m_serialPort;
    Ax25Kiss::KissStreamDecoder m_frameDecoder;
    QString m_localCallsign;
    ReceiveHandler m_receiveHandler;
    bool m_isConnected = false;
};

#endif // AX25KISSSERIALTRANSPORT_H
