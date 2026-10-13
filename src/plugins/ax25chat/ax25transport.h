#ifndef AX25TRANSPORT_H
#define AX25TRANSPORT_H

#include <QByteArray>
#include <QString>

#include <functional>

namespace Axcp
{
class Transport
{
public:
    using ReceiveHandler = std::function<void(const QString &, const QByteArray &)>;

    virtual ~Transport() {}
    virtual bool send(const QString &destination, const QByteArray &payload) = 0;
    virtual void setReceiveHandler(ReceiveHandler handler) = 0;
};
}

#endif // AX25TRANSPORT_H
