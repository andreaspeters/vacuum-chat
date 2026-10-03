#ifndef MESHCOREBLETRANSPORT_H
#define MESHCOREBLETRANSPORT_H

#include "meshcoretransport.h"
#include "meshcoreblelifecycle.h"

#include <QBluetoothAddress>
#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothDeviceInfo>
#include <QBluetoothUuid>
#include <QLowEnergyCharacteristic>
#include <QLowEnergyController>
#include <QLowEnergyDescriptor>
#include <QLowEnergyService>
#include <QQueue>
#include <QTimer>

class MeshCoreBleTransport : public MeshCoreTransport
{
    Q_OBJECT

public:
    explicit MeshCoreBleTransport(QObject *parent = nullptr);
    ~MeshCoreBleTransport() override;

    bool open(const QString &endpoint) override;
    void close() override;
    bool isConnected() const override;
    bool sendPayload(const QByteArray &payload) override;

private slots:
    void onDeviceDiscovered(const QBluetoothDeviceInfo &device);
    void onDiscoveryFinished();
    void onDiscoveryError(QBluetoothDeviceDiscoveryAgent::Error error);
    void onControllerConnected();
    void onControllerDisconnected();
    void onControllerDestroyed();
    void onControllerDiscoveryFinished();
    void onControllerError(QLowEnergyController::Error error);
    void onServiceDiscovered(const QBluetoothUuid &serviceUuid);
    void onServiceStateChanged(QLowEnergyService::ServiceState state);
    void onServiceError(QLowEnergyService::ServiceError error);
    void onCharacteristicChanged(const QLowEnergyCharacteristic &characteristic,
                                 const QByteArray &value);
    void onCharacteristicWritten(const QLowEnergyCharacteristic &characteristic,
                                 const QByteArray &value);
    void onDescriptorWritten(const QLowEnergyDescriptor &descriptor,
                             const QByteArray &value);

private:
    void stopDiscovery();
    bool startOpen(const QString &addressText);
    void setupCharacteristics();
    void processNextWrite();
    void fail(const QString &message);
    void cleanup();

    MeshCoreBleLifecycle m_lifecycle;
    QString m_reopenEndpoint;
    QTimer m_reopenTimer;
    QBluetoothAddress m_targetAddress;
    QBluetoothDeviceDiscoveryAgent *m_discoveryAgent = nullptr;
    QLowEnergyController *m_controller = nullptr;
    QLowEnergyService *m_service = nullptr;
    QLowEnergyCharacteristic m_rxCharacteristic;
    QLowEnergyCharacteristic m_txCharacteristic;
    QLowEnergyDescriptor m_cccd;
    QByteArray m_notificationValue;
    QQueue<QByteArray> m_pendingWrites;
    bool m_notificationWritePending = false;
    bool m_isConnected = false;
    bool m_writeInProgress = false;
};

#endif // MESHCOREBLETRANSPORT_H
