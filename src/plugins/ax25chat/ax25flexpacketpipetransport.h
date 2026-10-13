#ifndef AX25FLEXPACKETPIPETRANSPORT_H
#define AX25FLEXPACKETPIPETRANSPORT_H

#include "ax25transport.h"

#include <QObject>
#include <QByteArray>
#include <QQueue>
#include <QString>

class QSocketNotifier;
class QTimer;

class Ax25FlexPacketPipeTransport : public QObject, public Axcp::Transport
{
    Q_OBJECT

public:
    explicit Ax25FlexPacketPipeTransport(QObject *parent = nullptr);
    explicit Ax25FlexPacketPipeTransport(const QString &pipeDirectory, QObject *parent = nullptr);
    ~Ax25FlexPacketPipeTransport() override;

    bool open(const QString &localCallsign, int channel);
    void close();
    bool isConnected() const;

    bool send(const QString &destination, const QByteArray &payload) override;
    void setReceiveHandler(ReceiveHandler handler) override;

signals:
    void connected();
    void disconnected();
    void transportError(const QString &error);

private slots:
    void onPipeReadable();
    void flushPipeInput();
    void writeNextRecord();

private:
    bool writeRecord(const QByteArray &payload);
    bool writePendingRecord(const QByteArray &record);
    void processPipeBuffer(bool flushLastRecord);
    void deliverPipeRecord(int channel, const QByteArray &base64Data);

    int m_readFd = -1;
    int m_writeFd = -1;
    int m_channel = -1;
    QString m_localCallsign;
    QString m_pipeDirectory = QStringLiteral("/tmp");
    QByteArray m_pipeBuffer;
    QByteArray m_monitorBuffer;
    QString m_monitorSource;
    QQueue<QByteArray> m_pendingRecords;
    QSocketNotifier *m_readNotifier = nullptr;
    QTimer *m_inputTimer = nullptr;
    QTimer *m_outputTimer = nullptr;
    ReceiveHandler m_receiveHandler;
};

#endif // AX25FLEXPACKETPIPETRANSPORT_H
