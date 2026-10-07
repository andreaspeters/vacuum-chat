#ifndef MESHCORE_H
#define MESHCORE_H

#include <QObject>
#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <interfaces/imessage.h>

class MeshCoreTransport;

class MeshCoreCodec
{
public:
    static QByteArray frameUsbMessage(const QByteArray &payload);
    static QByteArray parseUsbFrame(const QByteArray &data, int &length);
    static QByteArray encodeBlePayload(const QByteArray &payload);
    static QByteArray decodeBlePayload(const QByteArray &data);
    static bool isValidMeshCorePacket(const QByteArray &data);
    static bool isUsbFrame(const QByteArray &data);
    static bool isBleFrame(const QByteArray &data);
};

struct MeshCoreDevice
{
    enum DeviceType { Unknown = 0, Node, Relay, Gateway };
    QString deviceId;
    QString name;
    DeviceType deviceType = Unknown;
    qint64 lastSeen = 0;
    bool isActive = false;
};

struct MeshCoreContact
{
    QString id;
    QString name;
    QString device;
    qint64 lastSeen = 0;
    bool isActive = false;
};

struct MeshCoreChannel
{
    QString id;
    QString name;
    QString topic;
    QStringList members;
};

class MeshCoreProtocol : public QObject
{
    Q_OBJECT

public:
    enum State { Idle = 0, Connecting, Connected, Disconnected };
    enum DeviceState { UnknownDevice = 0, DeviceReady, DeviceOffline, DeviceError };

    explicit MeshCoreProtocol(QObject *parent = nullptr);
    ~MeshCoreProtocol() override;

    bool initialize(const QString &configPath);
    bool connectToDevice();
    bool disconnect();
    QList<MeshCoreDevice> discoverDevices() const;
    QList<MeshCoreContact> discoverContacts() const;
    QList<MeshCoreChannel> discoverChannels() const;
    QList<int> availableChannelSlots() const;
    bool sendMessage(const QString &recipient, const QString &message);
    bool sendBroadcastMessage(const QString &message);
    bool sendGroupMessage(const QString &channelName, const QString &message);
    QList<BasicMessage> conversationHistory(const QString &conversationId) const;

    State getProtocolState() const { return protocolState; }
    DeviceState getDeviceState() const { return deviceState; }
    QString getBackend() const { return backend; }
    void setBackend(const QString &backend) { this->backend = backend; }
    QString localPublicKeyHex() const { return currentPublicKeyHex; }

public slots:
    bool setChannel(int channelIndex, const QString &name, const QByteArray &secret);
    bool addContact(const QString &publicKeyHex, const QString &name);

signals:
    void messageReceived(const BasicMessage &message);
    void deviceStateChanged(MeshCoreProtocol::DeviceState state);
    void connected();
    void disconnected();
    void contactsChanged();
    void channelsChanged();
    void channelConfigurationFinished(int channelIndex, bool success, const QString &error);
    void contactAdditionFinished(const QString &publicKeyHex, bool success,
                                 const QString &error);

protected:
    // Factory seam lets protocol tests supply a fake transport without hardware.
    virtual MeshCoreTransport* createTransport(const QString &backend) const;

private:
    enum ManagementCommand { NoManagementCommand, SetChannelCommand, AddContactCommand };

    // Handle transport signals and update protocol state
    void onTransportConnected();
    void onTransportDisconnected();
    void onPacketReceived(const QByteArray &payload);
    void onTransportError(const QString &message);
    bool sendAppStart();
    void handlePacket(const QByteArray &payload);
    void abortContactSync();
    void beginChannelSync();
    void requestNextChannel();
    void abortChannelSync();
    void advanceChannelSync();
    void handleChannelInfo(const QByteArray &packet);
    void handleChannelError();
    bool managementCommandReady() const;
    void finishManagementCommand(bool success, const QString &error);
    void startMessageSync();
    void requestNextMessage();
    void abortMessageSync();
    QString messageHistoryDirectory() const;
    bool persistMessage(const BasicMessage &message, bool &inserted) const;
    void removePersistedMessage(const BasicMessage &message) const;

    QString backend;
    QString devicePath;
    QString deviceMacAddress;
    QString currentPublicKeyHex;
    State protocolState = Idle;
    DeviceState deviceState = UnknownDevice;
    MeshCoreTransport* transport = nullptr;
    QList<MeshCoreContact> contacts;
    QList<MeshCoreContact> pendingContacts;
    QList<BasicMessage> deferredMessages;
    QList<MeshCoreChannel> channels;
    QList<MeshCoreChannel> pendingChannels;
    QList<int> freeChannelSlots;
    QList<int> pendingFreeChannelSlots;
    quint32 expectedContactCount = 0;
    quint8 requestedChannelIndex = 0;
    bool channelSyncInProgress = false;
    bool awaitingChannelInfo = false;
    bool messageSyncInProgress = false;
    bool messagesWaiting = false;
    bool appStartSent = false;
    bool awaitingSelfInfo = false;
    bool awaitingContactListStart = false;
    bool receivingContactList = false;
    ManagementCommand pendingManagementCommand = NoManagementCommand;
    bool awaitingManagementCommandResult = false;
    int pendingChannelIndex = -1;
    QString pendingContactPublicKey;
};

#endif // MESHCORE_H
