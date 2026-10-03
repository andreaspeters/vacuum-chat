#ifndef MESHCORETRANSPORT_H
#define MESHCORETRANSPORT_H

#include <QByteArray>
#include <QObject>
#include <QString>

// Transports normalize their link-specific framing and deliver raw Companion
// payloads through packetReceived(). sendPayload() accepts those raw payloads;
// each backend applies only the framing required by its physical link.
class MeshCoreTransport : public QObject
{
    Q_OBJECT

public:
    explicit MeshCoreTransport(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~MeshCoreTransport() override = default;

    // Starts a connection attempt. BLE completes asynchronously; serial may
    // emit connected() before this call returns.
    virtual bool open(const QString &endpoint) = 0;
    virtual void close() = 0;
    virtual bool isConnected() const = 0;
    virtual bool sendPayload(const QByteArray &payload) = 0;

signals:
    void connected();
    void disconnected();
    void packetReceived(const QByteArray &payload);
    void transportError(const QString &message);
};

#endif // MESHCORETRANSPORT_H
