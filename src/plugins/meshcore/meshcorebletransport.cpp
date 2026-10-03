#include "meshcorebletransport.h"

#include <QRegularExpression>

namespace
{
const QBluetoothUuid MeshCoreUartServiceUuid(
    QStringLiteral("{6E400001-B5A3-F393-E0A9-E50E24DCCA9E}"));
const QBluetoothUuid MeshCoreUartRxUuid(
    QStringLiteral("{6E400002-B5A3-F393-E0A9-E50E24DCCA9E}"));
const QBluetoothUuid MeshCoreUartTxUuid(
    QStringLiteral("{6E400003-B5A3-F393-E0A9-E50E24DCCA9E}"));
}

MeshCoreBleTransport::MeshCoreBleTransport(QObject *parent)
    : MeshCoreTransport(parent)
{
    m_reopenTimer.setSingleShot(true);
    connect(&m_reopenTimer, &QTimer::timeout, this, [this]() {
        const QString endpoint = m_reopenEndpoint;
        m_reopenEndpoint.clear();
        if (!endpoint.isEmpty())
            open(endpoint);
    });
}

MeshCoreBleTransport::~MeshCoreBleTransport()
{
    close();
}

bool MeshCoreBleTransport::open(const QString &endpoint)
{
    const QString addressText = endpoint.trimmed();
    static const QRegularExpression addressPattern(
        QStringLiteral("^[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}$"));
    if (!addressPattern.match(addressText).hasMatch()) {
        emit transportError(tr("Invalid BLE device address"));
        return false;
    }

    if (m_reopenTimer.isActive()) {
        m_reopenTimer.stop();
        m_reopenEndpoint.clear();
    }

    const MeshCoreBleLifecycle::OpenDisposition disposition = m_lifecycle.requestOpen();
    if (disposition == MeshCoreBleLifecycle::OpenDisposition::AlreadyActive) {
        if (!m_targetAddress.isNull() && m_targetAddress != QBluetoothAddress(addressText)) {
            emit transportError(tr("BLE transport is already active for another device"));
            return false;
        }
        return true;
    }
    if (disposition == MeshCoreBleLifecycle::OpenDisposition::Deferred) {
        m_reopenEndpoint = addressText;
        return true;
    }
    if (disposition != MeshCoreBleLifecycle::OpenDisposition::Start)
        return false;

    return startOpen(addressText);
}

bool MeshCoreBleTransport::startOpen(const QString &addressText)
{
    m_targetAddress = QBluetoothAddress(addressText);
    m_discoveryAgent = new QBluetoothDeviceDiscoveryAgent(this);
    connect(m_discoveryAgent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered,
            this, &MeshCoreBleTransport::onDeviceDiscovered);
    connect(m_discoveryAgent, &QBluetoothDeviceDiscoveryAgent::finished,
            this, &MeshCoreBleTransport::onDiscoveryFinished);
    connect(m_discoveryAgent, &QBluetoothDeviceDiscoveryAgent::errorOccurred,
            this, &MeshCoreBleTransport::onDiscoveryError);
    m_discoveryAgent->setLowEnergyDiscoveryTimeout(40000);
    QBluetoothDeviceDiscoveryAgent *agent = m_discoveryAgent;
    agent->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);

    if (m_discoveryAgent != agent)
        return false;
    if (!agent->isActive() &&
        agent->error() != QBluetoothDeviceDiscoveryAgent::NoError) {
        const QString message = agent->errorString();
        fail(message);
        return false;
    }
    return true;
}

void MeshCoreBleTransport::close()
{
    m_reopenTimer.stop();
    m_reopenEndpoint.clear();
    m_lifecycle.cancelPendingOpen();
    cleanup();
}

bool MeshCoreBleTransport::isConnected() const
{
    return m_isConnected && m_controller && m_service;
}

bool MeshCoreBleTransport::sendPayload(const QByteArray &payload)
{
    if (!isConnected() || !m_rxCharacteristic.isValid()) {
        emit transportError(tr("Cannot send data: BLE transport is not connected"));
        return false;
    }
    if (payload.isEmpty()) {
        emit transportError(tr("Cannot send an empty BLE payload"));
        return false;
    }
    if (m_pendingWrites.size() >= 32) {
        emit transportError(tr("BLE write queue is full"));
        return false;
    }

    m_pendingWrites.enqueue(payload);
    processNextWrite();
    return true;
}

void MeshCoreBleTransport::onDeviceDiscovered(const QBluetoothDeviceInfo &device)
{
    if (m_controller || device.address() != m_targetAddress ||
        !device.coreConfigurations().testFlag(QBluetoothDeviceInfo::LowEnergyCoreConfiguration))
        return;

    stopDiscovery();
    m_controller = QLowEnergyController::createCentral(device, this);
    if (!m_controller) {
        fail(tr("Could not create BLE central controller"));
        return;
    }

    connect(m_controller, &QLowEnergyController::connected,
            this, &MeshCoreBleTransport::onControllerConnected);
    connect(m_controller, &QLowEnergyController::disconnected,
            this, &MeshCoreBleTransport::onControllerDisconnected);
    connect(m_controller, &QLowEnergyController::serviceDiscovered,
            this, &MeshCoreBleTransport::onServiceDiscovered);
    connect(m_controller, &QLowEnergyController::discoveryFinished,
            this, &MeshCoreBleTransport::onControllerDiscoveryFinished);
    connect(m_controller, &QLowEnergyController::errorOccurred,
            this, &MeshCoreBleTransport::onControllerError);
    m_controller->connectToDevice();
}

void MeshCoreBleTransport::onDiscoveryFinished()
{
    if (!m_controller)
        fail(tr("Configured BLE device was not discovered"));
}

void MeshCoreBleTransport::onDiscoveryError(QBluetoothDeviceDiscoveryAgent::Error error)
{
    if (error == QBluetoothDeviceDiscoveryAgent::NoError)
        return;
    const QString message = m_discoveryAgent
        ? m_discoveryAgent->errorString()
        : tr("BLE device discovery failed");
    fail(message);
}

void MeshCoreBleTransport::onControllerConnected()
{
    if (m_controller)
        m_controller->discoverServices();
}

void MeshCoreBleTransport::onControllerDisconnected()
{
    if (m_isConnected)
        cleanup();
    else
        fail(tr("BLE device disconnected before the companion service was ready"));
}

void MeshCoreBleTransport::onControllerDestroyed()
{
    m_controller = nullptr;
    if (m_lifecycle.state() != MeshCoreBleLifecycle::State::Closing)
        return;

    const bool reopen = m_lifecycle.controllerDestroyed();
    if (reopen && !m_reopenEndpoint.isEmpty())
        m_reopenTimer.start(0);
    else
        m_reopenEndpoint.clear();
}

void MeshCoreBleTransport::onControllerDiscoveryFinished()
{
    if (!m_service)
        fail(tr("MeshCore UART service was not found"));
}

void MeshCoreBleTransport::onControllerError(QLowEnergyController::Error error)
{
    if (error == QLowEnergyController::NoError)
        return;
    const QString message = m_controller
        ? m_controller->errorString()
        : tr("BLE controller failed");
    fail(message);
}

void MeshCoreBleTransport::onServiceDiscovered(const QBluetoothUuid &serviceUuid)
{
    if (m_service || serviceUuid != MeshCoreUartServiceUuid || !m_controller)
        return;

    m_service = m_controller->createServiceObject(serviceUuid, this);
    if (!m_service) {
        fail(tr("Could not create MeshCore UART service"));
        return;
    }

    connect(m_service, &QLowEnergyService::stateChanged,
            this, &MeshCoreBleTransport::onServiceStateChanged);
    connect(m_service, &QLowEnergyService::errorOccurred,
            this, &MeshCoreBleTransport::onServiceError);
    connect(m_service, &QLowEnergyService::characteristicChanged,
            this, &MeshCoreBleTransport::onCharacteristicChanged);
    connect(m_service, &QLowEnergyService::characteristicWritten,
            this, &MeshCoreBleTransport::onCharacteristicWritten);
    connect(m_service, &QLowEnergyService::descriptorWritten,
            this, &MeshCoreBleTransport::onDescriptorWritten);
    m_service->discoverDetails(QLowEnergyService::SkipValueDiscovery);
}

void MeshCoreBleTransport::onServiceStateChanged(QLowEnergyService::ServiceState state)
{
    if (state == QLowEnergyService::RemoteServiceDiscovered)
        setupCharacteristics();
    else if (state == QLowEnergyService::InvalidService)
        fail(tr("MeshCore UART service became invalid"));
}

void MeshCoreBleTransport::onServiceError(QLowEnergyService::ServiceError error)
{
    if (error != QLowEnergyService::NoError)
        fail(tr("MeshCore GATT service operation failed (%1)").arg(static_cast<int>(error)));
}

void MeshCoreBleTransport::setupCharacteristics()
{
    if (!m_service || m_notificationWritePending || m_isConnected)
        return;

    m_rxCharacteristic = m_service->characteristic(MeshCoreUartRxUuid);
    m_txCharacteristic = m_service->characteristic(MeshCoreUartTxUuid);
    if (!m_rxCharacteristic.isValid() ||
        !m_rxCharacteristic.properties().testFlag(QLowEnergyCharacteristic::Write)) {
        fail(tr("MeshCore RX characteristic is missing or not writable"));
        return;
    }
    if (!m_txCharacteristic.isValid()) {
        fail(tr("MeshCore TX characteristic is missing"));
        return;
    }

    const auto txProperties = m_txCharacteristic.properties();
    if (txProperties.testFlag(QLowEnergyCharacteristic::Notify))
        m_notificationValue = QByteArray::fromHex("0100");
    else if (txProperties.testFlag(QLowEnergyCharacteristic::Indicate))
        m_notificationValue = QByteArray::fromHex("0200");
    else {
        fail(tr("MeshCore TX characteristic supports neither notifications nor indications"));
        return;
    }

    m_cccd = m_txCharacteristic.descriptor(
        QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
    if (!m_cccd.isValid()) {
        fail(tr("MeshCore TX characteristic has no client configuration descriptor"));
        return;
    }

    m_notificationWritePending = true;
    m_service->writeDescriptor(m_cccd, m_notificationValue);
}

void MeshCoreBleTransport::onCharacteristicChanged(
    const QLowEnergyCharacteristic &characteristic, const QByteArray &value)
{
    if (isConnected() && characteristic.uuid() == m_txCharacteristic.uuid())
        emit packetReceived(value);
}

void MeshCoreBleTransport::onCharacteristicWritten(
    const QLowEnergyCharacteristic &characteristic, const QByteArray &value)
{
    if (!m_writeInProgress || characteristic.uuid() != m_rxCharacteristic.uuid())
        return;
    if (m_pendingWrites.isEmpty() || m_pendingWrites.head() != value) {
        fail(tr("BLE write completion did not match the queued payload"));
        return;
    }

    m_pendingWrites.dequeue();
    m_writeInProgress = false;
    processNextWrite();
}

void MeshCoreBleTransport::onDescriptorWritten(
    const QLowEnergyDescriptor &descriptor, const QByteArray &value)
{
    if (!m_notificationWritePending || descriptor != m_cccd)
        return;
    if (value != m_notificationValue) {
        fail(tr("BLE notification subscription was rejected"));
        return;
    }

    m_notificationWritePending = false;
    m_isConnected = true;
    m_lifecycle.markConnected();
    emit connected();
    processNextWrite();
}

void MeshCoreBleTransport::processNextWrite()
{
    if (!isConnected() || m_writeInProgress || m_pendingWrites.isEmpty())
        return;

    m_writeInProgress = true;
    m_service->writeCharacteristic(m_rxCharacteristic, m_pendingWrites.head(),
                                   QLowEnergyService::WriteWithResponse);
}

void MeshCoreBleTransport::stopDiscovery()
{
    if (!m_discoveryAgent)
        return;

    disconnect(m_discoveryAgent, nullptr, this, nullptr);
    if (m_discoveryAgent->isActive())
        m_discoveryAgent->stop();
    m_discoveryAgent->deleteLater();
    m_discoveryAgent = nullptr;
}

void MeshCoreBleTransport::fail(const QString &message)
{
    emit transportError(message);
    cleanup();
}

void MeshCoreBleTransport::cleanup()
{
    const bool wasConnected = m_isConnected;
    m_isConnected = false;
    m_notificationWritePending = false;
    m_writeInProgress = false;
    m_pendingWrites.clear();

    stopDiscovery();

    if (m_service) {
        disconnect(m_service, nullptr, this, nullptr);
        m_service->deleteLater();
        m_service = nullptr;
    }
    if (m_controller) {
        QLowEnergyController *controller = m_controller;
        if (m_lifecycle.state() != MeshCoreBleLifecycle::State::Closing) {
            m_lifecycle.beginClose();
            disconnect(controller, nullptr, this, nullptr);
            connect(controller, &QObject::destroyed,
                    this, &MeshCoreBleTransport::onControllerDestroyed);
            connect(controller, &QLowEnergyController::stateChanged, controller,
                    [controller](QLowEnergyController::ControllerState state) {
                if (state == QLowEnergyController::UnconnectedState)
                    controller->deleteLater();
            });
            controller->setParent(nullptr);
            const QLowEnergyController::ControllerState state = controller->state();
            if (MeshCoreBleLifecycle::controllerMayBeDestroyed(
                    state == QLowEnergyController::UnconnectedState))
                controller->deleteLater();
            else if (state != QLowEnergyController::ClosingState)
                controller->disconnectFromDevice();
        }
    } else {
        m_lifecycle.reset();
        m_reopenEndpoint.clear();
    }

    m_rxCharacteristic = QLowEnergyCharacteristic();
    m_txCharacteristic = QLowEnergyCharacteristic();
    m_cccd = QLowEnergyDescriptor();
    m_notificationValue.clear();
    m_targetAddress = QBluetoothAddress();

    if (wasConnected)
        emit disconnected();
}
