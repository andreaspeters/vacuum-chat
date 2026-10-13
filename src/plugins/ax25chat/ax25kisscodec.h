#ifndef AX25KISSCODEC_H
#define AX25KISSCODEC_H

#include <QByteArray>
#include <QList>
#include <QString>
#include <QtGlobal>

namespace Ax25Kiss
{
class KissCodec
{
public:
    static const quint8 FEND = 0xc0;
    static const quint8 FESC = 0xdb;
    static const quint8 TFEND = 0xdc;
    static const quint8 TFESC = 0xdd;
    static const quint8 DataCommand = 0x00;

    static QByteArray encodeDataFrame(const QByteArray &payload, quint8 port = 0);
};

class KissStreamDecoder
{
public:
    explicit KissStreamDecoder(int maximumPayloadBytes = 1024, quint8 port = 0);

    QList<QByteArray> feed(const QByteArray &data);
    void reset();

private:
    int m_maximumPayloadBytes;
    quint8 m_port;
    QByteArray m_buffer;
    bool m_inFrame;
    bool m_escapePending;
    bool m_discardFrame;
};

struct Ax25UiFrame
{
    QString source;
    QString destination;
    QByteArray information;
};

class Ax25UiFrameCodec
{
public:
    static bool encodeUiFrame(const QString &sourceCallsign,
                              const QString &destinationCallsign,
                              const QByteArray &information,
                              QByteArray *frame,
                              QString *error = nullptr);
    static bool decodeUiFrame(const QByteArray &frame,
                              Ax25UiFrame *decoded,
                              QString *error = nullptr);
};
}

#endif // AX25KISSCODEC_H
