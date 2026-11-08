#include "meshcore.h"
#include "meshcoretransport.h"
#include "meshcoreserialtransport.h"
#ifdef MESHCORE_WITH_BLE
#include "meshcorebletransport.h"
#endif
#include "meshcorecodec.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <QtCore/QMessageAuthenticationCode>

#if HAVE_MESHCORE_OPENSSL
#include <openssl/evp.h>
#endif

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace
{
std::vector<std::uint8_t> toBytes(const QByteArray &data)
{
    std::vector<std::uint8_t> bytes;
    bytes.reserve(static_cast<std::size_t>(data.size()));
    for (char value : data)
        bytes.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(value)));
    return bytes;
}

QString extractSenderFromChannelText(const QString &text, QString &senderName)
{
    const int colonPos = text.indexOf(QLatin1String(": "));
    if (colonPos <= 0) {
        senderName.clear();
        return text;
    }

    senderName = text.left(colonPos);
    return text.mid(colonPos + 2);
}

void appendHashPart(QByteArray &identity, const QByteArray &part)
{
    identity.append(QByteArray::number(part.size()));
    identity.append(':');
    identity.append(part);
}

QString stableMessageId(const BasicMessage &message, const QByteArray &kind,
                        const QByteArray &senderPrefix, quint32 senderTimestamp)
{
    QByteArray identity;
    appendHashPart(identity, kind);
    appendHashPart(identity, message.conversationId().toUtf8());
    appendHashPart(identity, senderPrefix);
    appendHashPart(identity, QByteArray::number(senderTimestamp));
    appendHashPart(identity, message.body().toUtf8());
    return QStringLiteral("meshcore-") + QString::fromLatin1(
        QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
}

QString outgoingChannelCorrelationKey(quint8 channelIndex, quint32 timestamp,
                                      const QByteArray &wireText)
{
    QByteArray identity;
    identity.append(static_cast<char>(channelIndex));
    identity.append(static_cast<char>(timestamp & 0xff));
    identity.append(static_cast<char>((timestamp >> 8) & 0xff));
    identity.append(static_cast<char>((timestamp >> 16) & 0xff));
    identity.append(static_cast<char>((timestamp >> 24) & 0xff));
    identity.append(wireText);
    return QString::fromLatin1(QCryptographicHash::hash(
        identity, QCryptographicHash::Sha256).toHex());
}

QString repeaterCountLabel(int count)
{
    return QStringLiteral("%1 %2").arg(count)
        .arg(count == 1 ? QStringLiteral("Repeater") : QStringLiteral("Repeaters"));
}
}

QByteArray MeshCoreCodec::frameUsbMessage(const QByteArray &payload)
{
    if (payload.size() > 0xffff)
        return QByteArray();
    QByteArray frame;
    frame.reserve(payload.size() + 3);
    frame.append('<');
    frame.append(char(payload.size() & 0xff));
    frame.append(char((payload.size() >> 8) & 0xff));
    frame.append(payload);
    return frame;
}

QByteArray MeshCoreCodec::parseUsbFrame(const QByteArray &data, int &length)
{
    length = 0;
    if (data.size() < 3 || data.at(0) != '<')
        return QByteArray();
    length = static_cast<unsigned char>(data.at(1)) |
             (static_cast<unsigned char>(data.at(2)) << 8);
    if (data.size() < length + 3)
        return QByteArray();
    return data.mid(3, length);
}

bool MeshCoreCodec::isValidMeshCorePacket(const QByteArray &data)
{
    return !data.isEmpty() && (data.at(0) == '<' ? isUsbFrame(data) : isBleFrame(data));
}

bool MeshCoreCodec::isUsbFrame(const QByteArray &data)
{
    int length = 0;
    return !parseUsbFrame(data, length).isNull();
}

bool MeshCoreCodec::isBleFrame(const QByteArray &data) { return !data.isEmpty(); }

MeshCoreProtocol::MeshCoreProtocol(QObject *parent)
    : QObject(parent), deviceMacAddress(QStringLiteral("10:BD:A3:5A:6B:E9"))
{
    rosterSyncTimeoutTimer.setSingleShot(true);
    rosterSyncRetryTimer.setSingleShot(true);
    connect(&rosterSyncTimeoutTimer, &QTimer::timeout, this, [this]() {
        if (awaitingDeviceInfo) {
            awaitingDeviceInfo = false;
            qWarning() << "MeshCore DEVICE_QUERY timed out; continuing with channel discovery";
            beginChannelSync();
            return;
        }
        QString phase = QStringLiteral("unknown roster phase");
        if (awaitingSelfInfo)
            phase = QStringLiteral("SELF_INFO");
        else if (awaitingContactListStart)
            phase = QStringLiteral("CONTACTS_START");
        else if (receivingContactList)
            phase = QStringLiteral("CONTACT_LIST");
        else if (awaitingChannelInfo)
            phase = QStringLiteral("CHANNEL_INFO");
        retryRosterSync(QStringLiteral("%1 response timed out").arg(phase));
    });
    connect(&rosterSyncRetryTimer, &QTimer::timeout, this, [this]() {
        if (protocolState != Connected || !rosterSyncActive)
            return;
        if (awaitingSelfInfo) {
            appStartSent = false;
            awaitingSelfInfo = false;
            if (!sendAppStart())
                retryRosterSync(QStringLiteral("failed to resend Companion APP_START"));
        } else if (initialRosterSyncStage == InitialRosterSyncStage::Channels) {
            beginChannelSync();
        } else {
            startContactSync();
        }
    });
}

MeshCoreProtocol::~MeshCoreProtocol() { disconnect(); }

bool MeshCoreProtocol::initialize(const QString &configPath)
{
    currentPublicKeyHex.clear();
    // configPath is the transport endpoint: a serial device path for USB, or
    // the MAC/address for BLE. Default backend is USB until a backend is chosen.
    backend = "usb";
    devicePath = configPath;
    return true;
}

bool MeshCoreProtocol::connectToDevice()
{
    if (protocolState == Connecting || protocolState == Connected)
        return true;
    currentPublicKeyHex.clear();
    rosterSyncTimeoutTimer.stop();
    rosterSyncRetryTimer.stop();
    rosterSyncRetryCount = 0;
    rosterSyncActive = false;
    initialRosterSyncStage = InitialRosterSyncStage::None;

    const bool allowedBackend = backend == "usb" || backend == "ble";
    if (!allowedBackend) {
        protocolState = Disconnected;
        deviceState = DeviceError;
        emit deviceStateChanged(deviceState);
        qWarning() << "MeshCoreProtocol: invalid backend selected," << backend;
        return false;
    }

    if (!transport) {
        transport = createTransport(backend);
        if (!transport) {
            protocolState = Disconnected;
            deviceState = DeviceError;
            emit deviceStateChanged(deviceState);
            return false;
        }
        transport->setParent(this);

        connect(transport, &MeshCoreTransport::connected,
                this, &MeshCoreProtocol::onTransportConnected);
        connect(transport, &MeshCoreTransport::disconnected,
                this, &MeshCoreProtocol::onTransportDisconnected);
        connect(transport, &MeshCoreTransport::packetReceived,
                this, &MeshCoreProtocol::onPacketReceived);
        connect(transport, &MeshCoreTransport::transportError,
                this, &MeshCoreProtocol::onTransportError);
    }

    appStartSent = false;
    awaitingSelfInfo = false;
    awaitingDeviceInfo = false;
    awaitingContactListStart = false;
    receivingContactList = false;
    pendingContacts.clear();
    expectedContactCount = 0;
    abortChannelSync();
    abortMessageSync();
    messagesWaiting = true;
    protocolState = Connecting;

    if (!transport->open(devicePath)) {
        protocolState = Disconnected;
        deviceState = DeviceError;
        emit deviceStateChanged(deviceState);
        return false;
    }

    // Serial transports may already be connected without emitting a later event.
    // BLE normally reaches this state asynchronously through onTransportConnected().
    if (transport->isConnected() && protocolState == Connecting)
        onTransportConnected();

    return true;
}

bool MeshCoreProtocol::disconnect()
{
    rosterSyncTimeoutTimer.stop();
    rosterSyncRetryTimer.stop();
    rosterSyncActive = false;
    if (transport)
        transport->close();

    if (pendingManagementCommand != NoManagementCommand)
        finishManagementCommand(false,
            QStringLiteral("Device disconnected before the command completed"));

    const bool wasActive = protocolState == Connected || protocolState == Connecting;
    protocolState = Disconnected;
    deviceState = DeviceOffline;
    appStartSent = false;
    awaitingSelfInfo = false;
    awaitingDeviceInfo = false;
    abortContactSync();
    abortChannelSync();
    abortMessageSync();
    messagesWaiting = true;
    currentPublicKeyHex.clear();
    if (wasActive) {
        emit disconnected();
    }
    
    return true;
}

// Protected virtual factory implementation
MeshCoreTransport* MeshCoreProtocol::createTransport(const QString &backend) const
{
    if (backend == "usb") {
        return new MeshCoreSerialTransport();
    } else if (backend == "ble") {
#ifdef MESHCORE_WITH_BLE
        return new MeshCoreBleTransport();
#else
        // In OS/2, avoid building BLE support
        return nullptr;
#endif
    }
    return nullptr; // Invalid backend
}

// Signal handlers
void MeshCoreProtocol::onTransportConnected()
{
    if (protocolState != Connecting)
        return;

    protocolState = Connected;
    deviceState = DeviceReady;
    if (!sendAppStart()) {
        onTransportError(QStringLiteral("Failed to send Companion APP_START"));
        return;
    }

    emit connected();
    emit deviceStateChanged(deviceState);
}

bool MeshCoreProtocol::sendAppStart()
{
    if (appStartSent)
        return true;
    if (!transport || protocolState != Connected)
        return false;

    QByteArray command(8, '\0');
    command[0] = static_cast<char>(0x01);
    awaitingSelfInfo = true;
    if (!transport->sendPayload(command))
        return false;

    appStartSent = true;
    rosterSyncActive = true;
    rosterSyncTimeoutTimer.start(5000);
    return true;
}

void MeshCoreProtocol::abortContactSync()
{
    awaitingContactListStart = false;
    receivingContactList = false;
    expectedContactCount = 0;
    pendingContacts.clear();
}

void MeshCoreProtocol::startContactSync()
{
    if (protocolState != Connected || channelSyncInProgress || messageSyncInProgress || !transport ||
        awaitingSelfInfo || awaitingDeviceInfo || awaitingContactListStart || receivingContactList)
        return;

    // Reset sync state
    pendingContacts.clear();
    expectedContactCount = 0;
    awaitingContactListStart = true;

    if (!transport->sendPayload(QByteArray(1, static_cast<char>(0x04)))) {
        abortContactSync();
        retryRosterSync(QStringLiteral("failed to request Companion contacts"));
        return;
    }
    rosterSyncActive = true;
    rosterSyncTimeoutTimer.start(5000);
}

void MeshCoreProtocol::retryRosterSync(const QString &reason)
{
    if (protocolState != Connected || !transport)
        return;

    rosterSyncTimeoutTimer.stop();
    rosterSyncRetryTimer.stop();
    awaitingDeviceInfo = false;
    abortContactSync();
    abortChannelSync();
    if (rosterSyncRetryCount >= 3) {
        rosterSyncActive = false;
        qWarning() << "MeshCore roster sync stopped after 3 retries:" << reason;
        if (pendingManagementCommand != NoManagementCommand)
            finishManagementCommand(false, reason);
        return;
    }

    ++rosterSyncRetryCount;
    qWarning() << "Retrying MeshCore roster sync" << rosterSyncRetryCount << "of 3:" << reason;
    rosterSyncRetryTimer.start(1000);
}

void MeshCoreProtocol::finishRosterSync()
{
    rosterSyncTimeoutTimer.stop();
    rosterSyncRetryTimer.stop();
    rosterSyncRetryCount = 0;
    rosterSyncActive = false;
    awaitingDeviceInfo = false;
    initialRosterSyncStage = InitialRosterSyncStage::None;
}

void MeshCoreProtocol::beginChannelSync()
{
    channelSyncInProgress = true;
    awaitingChannelInfo = false;
    requestedChannelIndex = 0;
    pendingChannels.clear();
    pendingFreeChannelSlots.clear();
    requestNextChannel();
}

void MeshCoreProtocol::abortChannelSync()
{
    channelSyncInProgress = false;
    awaitingChannelInfo = false;
    requestedChannelIndex = 0;
    pendingChannels.clear();
    pendingFreeChannelSlots.clear();
}

void MeshCoreProtocol::requestNextChannel()
{
    if (!channelSyncInProgress || !transport || protocolState != Connected)
        return;

    if (requestedChannelIndex >= 8) {
        channels = pendingChannels;
        freeChannelSlots = pendingFreeChannelSlots;
        qWarning() << "[MC-SYNC] channel catalog published; rooms" << channels.size();
        abortChannelSync();
        emit channelsChanged();
        if (initialRosterSyncStage == InitialRosterSyncStage::Channels) {
            initialRosterSyncStage = InitialRosterSyncStage::Contacts;
            startContactSync();
            return;
        }
        finishRosterSync();
        if ((pendingManagementCommand == SetChannelCommand ||
             pendingManagementCommand == AddContactCommand) &&
            !awaitingManagementCommandResult)
            finishManagementCommand(true, QString());
        messagesWaiting = true;
        startMessageSync();
        return;
    }

    QByteArray command;
    command.append(static_cast<char>(0x1f)); // CMD_GET_CHANNEL
    command.append(static_cast<char>(requestedChannelIndex));
    awaitingChannelInfo = true;
    rosterSyncTimeoutTimer.start(5000);
    if (!transport->sendPayload(command)) {
        abortChannelSync();
        if (pendingManagementCommand != NoManagementCommand &&
            !awaitingManagementCommandResult)
            finishManagementCommand(false,
                QStringLiteral("Failed to refresh MeshCore channels after the update"));
        onTransportError(QStringLiteral("Failed to request MeshCore channel info"));
    }
}

void MeshCoreProtocol::publishChannelSlot(quint8 channelIndex)
{
    const QString channelId = QString::number(channelIndex);
    int currentIndex = -1;
    for (int index = 0; index < channels.size(); ++index) {
        if (channels.at(index).id == channelId) {
            currentIndex = index;
            break;
        }
    }

    int pendingIndex = -1;
    for (int index = 0; index < pendingChannels.size(); ++index) {
        if (pendingChannels.at(index).id == channelId) {
            pendingIndex = index;
            break;
        }
    }

    bool changed = false;
    if (currentIndex >= 0 &&
        (pendingIndex < 0 || channels.at(currentIndex).name != pendingChannels.at(pendingIndex).name)) {
        channels.removeAt(currentIndex);
        currentIndex = -1;
        changed = true;
    }
    if (pendingIndex >= 0 && currentIndex < 0) {
        const MeshCoreChannel channel = pendingChannels.at(pendingIndex);
        int insertAt = 0;
        while (insertAt < channels.size() && channels.at(insertAt).id.toInt() < channelIndex)
            ++insertAt;
        channels.insert(insertAt, channel);
        changed = true;
    }
    if (changed)
        emit channelsChanged();
}

void MeshCoreProtocol::advanceChannelSync()
{
    awaitingChannelInfo = false;
    ++requestedChannelIndex;
    requestNextChannel();
}

void MeshCoreProtocol::handleChannelInfo(const QByteArray &packet)
{
    if (packet.size() < 50 ||
        static_cast<quint8>(packet.at(1)) != requestedChannelIndex) {
        retryRosterSync(QStringLiteral("MeshCore returned an invalid channel catalog response"));
        return;
    }

    int nameLength = 0;
    while (nameLength < 32 && packet.at(2 + nameLength) != '\0')
        ++nameLength;
    const QString name = QString::fromUtf8(packet.constData() + 2, nameLength);
    if (!name.isEmpty()) {
        MeshCoreChannel channel;
        channel.id = QString::number(requestedChannelIndex);
        channel.name = name;
        channel.secret = packet.mid(34, 16);
        pendingChannels.append(channel);
    } else {
        bool hasZeroSecret = true;
        for (int index = 34; index < 50; ++index) {
            if (packet.at(index) != '\0') {
                hasZeroSecret = false;
                break;
            }
        }
        if (hasZeroSecret)
            pendingFreeChannelSlots.append(requestedChannelIndex);
    }
    publishChannelSlot(requestedChannelIndex);
    advanceChannelSync();
}

void MeshCoreProtocol::handleChannelError()
{
    // A missing channel index is a valid response when probing all eight slots.
    pendingFreeChannelSlots.append(requestedChannelIndex);
    publishChannelSlot(requestedChannelIndex);
    advanceChannelSync();
}

bool MeshCoreProtocol::managementCommandReady() const
{
    return protocolState == Connected && transport &&
        pendingManagementCommand == NoManagementCommand &&
        !awaitingSelfInfo && !awaitingContactListStart && !receivingContactList &&
        !channelSyncInProgress && !awaitingChannelInfo && !messageSyncInProgress;
}

void MeshCoreProtocol::finishManagementCommand(bool success, const QString &error)
{
    const ManagementCommand command = pendingManagementCommand;
    const int channelIndex = pendingChannelIndex;
    const QString contactPublicKey = pendingContactPublicKey;
    pendingManagementCommand = NoManagementCommand;
    awaitingManagementCommandResult = false;
    pendingChannelIndex = -1;
    pendingContactPublicKey.clear();

    if (command == SetChannelCommand)
        emit channelConfigurationFinished(channelIndex, success, error);
    else if (command == AddContactCommand)
        emit contactAdditionFinished(contactPublicKey, success, error);
}

bool MeshCoreProtocol::setChannel(int channelIndex, const QString &name,
                                  const QByteArray &secret)
{
    if (!managementCommandReady() || channelIndex < 0 || channelIndex > 7 ||
        secret.size() != 16)
        return false;

    const QByteArray utf8Name = name.toUtf8();
    const std::vector<std::uint8_t> encoded = MeshCoreCompanionCodec::encodeSetChannel(
        static_cast<std::uint8_t>(channelIndex),
        std::string(utf8Name.constData(), static_cast<std::size_t>(utf8Name.size())),
        toBytes(secret));
    if (encoded.empty())
        return false;

    const QByteArray command(reinterpret_cast<const char *>(encoded.data()),
                             static_cast<int>(encoded.size()));
    pendingManagementCommand = SetChannelCommand;
    pendingChannelIndex = channelIndex;
    awaitingManagementCommandResult = true;
    if (!transport->sendPayload(command)) {
        finishManagementCommand(false,
            QStringLiteral("Failed to send MeshCore channel update"));
        onTransportError(QStringLiteral("Failed to send MeshCore channel update"));
        return false;
    }
    return true;
}

bool MeshCoreProtocol::addContact(const QString &publicKeyHex, const QString &name)
{
    if (!managementCommandReady())
        return false;

    const QString canonicalPublicKey = publicKeyHex.toLower();
    const QByteArray utf8Name = name.toUtf8();
    const std::vector<std::uint8_t> encoded = MeshCoreCompanionCodec::encodeAddUpdateContact(
        canonicalPublicKey.toLatin1().toStdString(), 0x01, 0x00, -1, -1, "",
        std::string(utf8Name.constData(), static_cast<std::size_t>(utf8Name.size())),
        0, 0, 0);
    if (encoded.empty())
        return false;

    const QByteArray command(reinterpret_cast<const char *>(encoded.data()),
                             static_cast<int>(encoded.size()));
    pendingManagementCommand = AddContactCommand;
    pendingContactPublicKey = canonicalPublicKey;
    awaitingManagementCommandResult = true;
    if (!transport->sendPayload(command)) {
        finishManagementCommand(false,
            QStringLiteral("Failed to send MeshCore contact update"));
        onTransportError(QStringLiteral("Failed to send MeshCore contact update"));
        return false;
    }
    return true;
}

bool MeshCoreProtocol::sendSelfAdvert(bool flood)
{
    if (!managementCommandReady())
        return false;

    // CMD_SEND_SELF_ADVERT (0x07): type 0 is zero-hop; type 1 is flood.
    QByteArray command;
    command.append(static_cast<char>(0x07));
    command.append(static_cast<char>(flood ? 0x01 : 0x00));
    pendingManagementCommand = SendSelfAdvertCommand;
    awaitingManagementCommandResult = true;
    if (!transport->sendPayload(command)) {
        finishManagementCommand(false,
            QStringLiteral("Failed to send MeshCore self-advert command"));
        onTransportError(QStringLiteral("Failed to send MeshCore self-advert command"));
        return false;
    }
    return true;
}

QString MeshCoreProtocol::messageHistoryDirectory() const
{
    const QString dataDirectory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return dataDirectory.isEmpty()
        ? QString()
        : QDir(dataDirectory).filePath(QStringLiteral("meshcore/messages"));
}

bool MeshCoreProtocol::persistMessage(const BasicMessage &message, bool &inserted) const
{
    inserted = false;
    if (message.messageId().isEmpty() || message.conversationId().isEmpty())
        return false;

    const QString root = messageHistoryDirectory();
    if (root.isEmpty())
        return false;

    const QString conversationDirectory = QDir(root).filePath(QString::fromLatin1(
        QCryptographicHash::hash(message.conversationId().toUtf8(),
                                 QCryptographicHash::Sha256).toHex()));
    if (!QDir().mkpath(conversationDirectory))
        return false;

    const QString path = QDir(conversationDirectory).filePath(message.messageId() + QStringLiteral(".json"));
    QJsonObject object;
    const bool existed = QFile::exists(path);
    if (existed) {
        QFile existingFile(path);
        if (!existingFile.open(QIODevice::ReadOnly))
            return false;
        const QJsonDocument existingDocument = QJsonDocument::fromJson(existingFile.readAll());
        if (!existingDocument.isObject() ||
            existingDocument.object().value(QStringLiteral("conversation_id")).toString() !=
                message.conversationId() ||
            existingDocument.object().value(QStringLiteral("message_id")).toString() !=
                message.messageId())
            return false;
        object = existingDocument.object();
    }
    object.insert(QStringLiteral("message_id"), message.messageId());
    object.insert(QStringLiteral("conversation_id"), message.conversationId());
    object.insert(QStringLiteral("sender"), message.sender());
    object.insert(QStringLiteral("recipient"), message.recipient());
    object.insert(QStringLiteral("body"), message.body());
    object.insert(QStringLiteral("timestamp"), message.timestamp().toUTC().toString(Qt::ISODateWithMs));
    object.insert(QStringLiteral("protocol"), message.protocol());
    object.insert(QStringLiteral("direction"), static_cast<int>(message.direction()));
    object.insert(QStringLiteral("metadata"), QJsonObject::fromVariantMap(message.metadata()));

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const QByteArray contents = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (file.write(contents) != contents.size() || !file.commit())
        return false;
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    inserted = !existed;
    return true;
}

void MeshCoreProtocol::removePersistedMessage(const BasicMessage &message) const
{
    const QString root = messageHistoryDirectory();
    if (root.isEmpty() || message.messageId().isEmpty() || message.conversationId().isEmpty())
        return;
    const QString conversationDirectory = QDir(root).filePath(QString::fromLatin1(
        QCryptographicHash::hash(message.conversationId().toUtf8(),
                                 QCryptographicHash::Sha256).toHex()));
    QFile::remove(QDir(conversationDirectory).filePath(message.messageId() + QStringLiteral(".json")));
}

QList<BasicMessage> MeshCoreProtocol::conversationHistory(const QString &conversationId) const
{
    QList<BasicMessage> result;
    if (conversationId.isEmpty())
        return result;

    const QString root = messageHistoryDirectory();
    if (root.isEmpty())
        return result;
    const QString directory = QDir(root).filePath(QString::fromLatin1(
        QCryptographicHash::hash(conversationId.toUtf8(), QCryptographicHash::Sha256).toHex()));
    QDirIterator files(directory, {QStringLiteral("*.json")}, QDir::Files);
    while (files.hasNext()) {
        QFile file(files.next());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        if (!document.isObject())
            continue;
        const QJsonObject object = document.object();
        if (object.value(QStringLiteral("conversation_id")).toString() != conversationId)
            continue;
        const int direction = object.value(QStringLiteral("direction")).toInt(-1);
        if (direction < static_cast<int>(BasicMessage::Incoming) ||
            direction > static_cast<int>(BasicMessage::System))
            continue;
        const QDateTime timestamp = QDateTime::fromString(
            object.value(QStringLiteral("timestamp")).toString(), Qt::ISODateWithMs);
        if (!timestamp.isValid())
            continue;
        QString sender = object.value(QStringLiteral("sender")).toString();
        QString body = object.value(QStringLiteral("body")).toString();
        if (conversationId.startsWith(QStringLiteral("channel:")) &&
            direction == static_cast<int>(BasicMessage::Incoming)) {
            if (sender.isEmpty())
                body = extractSenderFromChannelText(body, sender);
        }
        BasicMessage message(
            object.value(QStringLiteral("message_id")).toString(), conversationId,
            sender, object.value(QStringLiteral("recipient")).toString(), body, timestamp,
            object.value(QStringLiteral("protocol")).toString(),
            static_cast<BasicMessage::Direction>(direction));
        const QJsonObject metadata = object.value(QStringLiteral("metadata")).toObject();
        if (!metadata.isEmpty())
            message.setMetadata(metadata.toVariantMap());
        result.append(message);
    }

    std::sort(result.begin(), result.end(), [](const BasicMessage &left, const BasicMessage &right) {
        if (left.timestamp() != right.timestamp())
            return left.timestamp() < right.timestamp();
        return left.messageId() < right.messageId();
    });
    return result;
}

void MeshCoreProtocol::startMessageSync()
{
    if (protocolState != Connected || channelSyncInProgress || messageSyncInProgress || !transport ||
        awaitingSelfInfo || awaitingContactListStart || receivingContactList)
        return;
    if (!messagesWaiting)
        return;

    const QList<BasicMessage> pendingMessages = deferredMessages;
    for (const BasicMessage &message : pendingMessages) {
        bool inserted = false;
        if (!persistMessage(message, inserted)) {
            messagesWaiting = true;
            onTransportError(QStringLiteral("Could not persist queued MeshCore message"));
            return;
        }
        if (inserted)
            emit messageReceived(message);
    }
    deferredMessages.clear();

    messagesWaiting = false;
    messageSyncInProgress = true;
    requestNextMessage();
}

void MeshCoreProtocol::requestNextMessage()
{
    if (!messageSyncInProgress || !transport || protocolState != Connected)
        return;
    if (!transport->sendPayload(QByteArray(1, static_cast<char>(0x0a)))) {
        messageSyncInProgress = false;
        messagesWaiting = true;
        onTransportError(QStringLiteral("Failed to request next MeshCore message"));
    }
}

void MeshCoreProtocol::abortMessageSync()
{
    messageSyncInProgress = false;
}

void MeshCoreProtocol::handlePacket(const QByteArray &payload)
{
    if (payload.isEmpty() || protocolState != Connected)
        return;

    const std::vector<std::uint8_t> packet = toBytes(payload);
    const std::uint8_t packetType = packet.front();
    if (packetType == 0x88) {
        handleRxLog(payload);
        return;
    }
    if (packetType == 0x05 && awaitingSelfInfo) {
        MeshCoreSelfInfo selfInfo;
        if (!MeshCoreCompanionCodec::decodeSelfInfo(packet, selfInfo)) {
            qWarning() << "[MC-SYNC] SELF_INFO decode failed; payload bytes" << payload.size();
            return;
        }

        awaitingSelfInfo = false;
        initialRosterSyncStage = InitialRosterSyncStage::Channels;
        // Store the public key from SELF_INFO packet
        currentPublicKeyHex = QString::fromStdString(selfInfo.publicKeyHex);
        localNodeName = QString::fromUtf8(selfInfo.name.data(),
            static_cast<int>(selfInfo.name.size())).section(QChar('\0'), 0, 0).trimmed();
        QByteArray deviceQuery = QByteArray::fromHex("1603"); // CMD_DEVICE_QUERY protocol version 3
        awaitingDeviceInfo = true;
        if (!transport->sendPayload(deviceQuery)) {
            awaitingDeviceInfo = false;
            retryRosterSync(QStringLiteral("failed to query MeshCore device info"));
            return;
        }
        qWarning() << "[MC-SYNC] SELF_INFO accepted; DEVICE_QUERY sent; payload bytes"
                   << payload.size();
        rosterSyncTimeoutTimer.start(5000);
        return;
    }

    if (awaitingDeviceInfo && packetType == 0x0d) {
        qWarning() << "[MC-SYNC] DEVICE_INFO received; payload bytes" << payload.size();
        awaitingDeviceInfo = false;
        rosterSyncTimeoutTimer.stop();
        beginChannelSync();
        return;
    }

    if (awaitingDeviceInfo && packetType == 0x01) {
        const int errorCode = payload.size() > 1
            ? static_cast<quint8>(payload.at(1)) : 0;
        qWarning() << "MeshCore rejected DEVICE_QUERY (error" << errorCode
                   << "); continuing with channel discovery";
        awaitingDeviceInfo = false;
        rosterSyncTimeoutTimer.stop();
        beginChannelSync();
        return;
    }

    if (awaitingManagementCommandResult &&
        (packetType == 0x00 || packetType == 0x01)) {
        if (packetType == 0x01) {
            const int errorCode = payload.size() > 1
                ? static_cast<quint8>(payload.at(1)) : 0;
            finishManagementCommand(false,
                QStringLiteral("MeshCore rejected the management command (error %1)")
                    .arg(errorCode));
        } else {
            awaitingManagementCommandResult = false;
            if (pendingManagementCommand == SetChannelCommand) {
                beginChannelSync();
            } else if (pendingManagementCommand == AddContactCommand) {
                startContactSync();
            } else if (pendingManagementCommand == SendSelfAdvertCommand) {
                finishManagementCommand(true, QString());
            }
        }
        return;
    }

    if (packetType == 0x02 && awaitingContactListStart) {
        std::uint32_t contactCount = 0;
        if (!MeshCoreCompanionCodec::decodeContactListStart(packet, contactCount)) {
            retryRosterSync(QStringLiteral("MeshCore returned an invalid contact list start"));
            return;
        }

        expectedContactCount = contactCount;
        qWarning() << "[MC-SYNC] contact list started; expected contacts" << contactCount;
        pendingContacts.clear();
        awaitingContactListStart = false;
        receivingContactList = true;
        rosterSyncTimeoutTimer.start(5000);
        return;
    }

    if (packetType == 0x03 && receivingContactList) {
        if (static_cast<std::uint32_t>(pendingContacts.size()) >= expectedContactCount) {
            retryRosterSync(QStringLiteral("MeshCore returned too many contacts"));
            return;
        }

        MeshCoreContactRecord record;
        if (!MeshCoreCompanionCodec::decodeContactRecord(packet, record)) {
            retryRosterSync(QStringLiteral("MeshCore returned an invalid contact record"));
            return;
        }

        MeshCoreContact contact;
        contact.id = QString::fromStdString(record.publicKeyHex);
        contact.name = QString::fromUtf8(record.advertisedName.data(),
                                         static_cast<int>(record.advertisedName.size()));
        contact.lastSeen = static_cast<qint64>(record.lastAdvert);
        pendingContacts.append(contact);
        bool foundContact = false;
        bool contactChanged = true;
        for (MeshCoreContact &existing : contacts) {
            if (existing.id != contact.id)
                continue;
            foundContact = true;
            contactChanged = existing.name != contact.name || existing.lastSeen != contact.lastSeen;
            if (contactChanged)
                existing = contact;
            break;
        }
        if (!foundContact)
            contacts.append(contact);
        if (contactChanged)
            emit contactsChanged();
        rosterSyncTimeoutTimer.start(5000);
        return;
    }

    if (packetType == 0x04 && receivingContactList) {
        std::uint32_t lastModified = 0;
        if (!MeshCoreCompanionCodec::decodeContactListEnd(packet, lastModified) ||
            static_cast<std::uint32_t>(pendingContacts.size()) != expectedContactCount) {
            retryRosterSync(QStringLiteral("MeshCore returned an incomplete contact list"));
            return;
        }

        Q_UNUSED(lastModified);
        bool contactsChangedAtEnd = contacts.size() != pendingContacts.size();
        if (!contactsChangedAtEnd) {
            for (int index = 0; index < contacts.size(); ++index) {
                const MeshCoreContact &current = contacts.at(index);
                const MeshCoreContact &pending = pendingContacts.at(index);
                if (current.id != pending.id || current.name != pending.name ||
                    current.lastSeen != pending.lastSeen) {
                    contactsChangedAtEnd = true;
                    break;
                }
            }
        }
        contacts = pendingContacts;
        qWarning() << "[MC-SYNC] contact list published; contacts" << contacts.size();
        abortContactSync();
        rosterSyncTimeoutTimer.stop();
        if (contactsChangedAtEnd)
            emit contactsChanged();
        if (initialRosterSyncStage == InitialRosterSyncStage::Contacts) {
            finishRosterSync();
            messagesWaiting = true;
            startMessageSync();
        } else {
            beginChannelSync();
        }
        return;
    }

    if (packetType == 0x83) {
        messagesWaiting = true;
        startMessageSync();
        return;
    }

    if (channelSyncInProgress && awaitingChannelInfo) {
        if (packetType == 0x12) {
            handleChannelInfo(payload);
            return;
        }
        if (packetType == 0x01) {
            handleChannelError();
            return;
        }
    }

    if (!messageSyncInProgress)
        return;

    if (packetType == 0x0a) {
        abortMessageSync();
        if (messagesWaiting)
            startMessageSync();
        return;
    }
    if (packetType == 0x1b) {
        requestNextMessage();
        return;
    }

    if (packetType != 0x07 && packetType != 0x08 &&
        packetType != 0x10 && packetType != 0x11)
        return;

    MeshCoreIncomingText incomingText;
    if (!MeshCoreCompanionCodec::decodeIncomingText(packet, incomingText) ||
        (incomingText.textType != 0 && incomingText.textType != 2) ||
        (incomingText.isChannel && incomingText.channelIndex >= 8)) {
        qWarning() << "Skipping malformed or unsupported MeshCore queued text item";
        requestNextMessage();
        return;
    }

    const QByteArray senderPrefix = QByteArray::fromStdString(
        incomingText.senderPrefixHex).toLower();
    QString conversationId;
    QString sender;
    if (incomingText.isChannel) {
        conversationId = QStringLiteral("channel:") + QString::number(incomingText.channelIndex);
    } else {
        QString matchingContact;
        int matchingContacts = 0;
        for (const MeshCoreContact &contact : contacts) {
            if (contact.id.startsWith(QString::fromLatin1(senderPrefix), Qt::CaseInsensitive)) {
                matchingContact = contact.id;
                ++matchingContacts;
            }
        }
        if (matchingContacts == 1) {
            conversationId = matchingContact;
            sender = matchingContact;
        } else {
            conversationId = QStringLiteral("contact-prefix:") + QString::fromLatin1(senderPrefix);
            sender = QString::fromLatin1(senderPrefix);
        }
    }

    const QString body = QString::fromUtf8(incomingText.text.data(),
                                           static_cast<int>(incomingText.text.size()));
    const QDateTime timestamp = QDateTime::fromSecsSinceEpoch(
        static_cast<qint64>(incomingText.senderTimestamp)).toUTC();

    // Process channel messages to extract sender from prefix
    QString senderName;
    QString bodyAfterSplit;
    if (incomingText.isChannel) {
        bodyAfterSplit = extractSenderFromChannelText(body, senderName);
    } else {
        bodyAfterSplit = body;
    }

    const QString messageSender = incomingText.isChannel ? senderName : sender;
    BasicMessage message(QString(), conversationId, messageSender, QString(), body, timestamp,
                          QStringLiteral("meshcore"), BasicMessage::Incoming);
    message.setMessageId(stableMessageId(message,
        incomingText.isChannel ? QByteArrayLiteral("channel") : QByteArrayLiteral("direct"),
        senderPrefix, incomingText.senderTimestamp));
    message.setBody(bodyAfterSplit);
    QVariantMap metadata;
    metadata.insert(QStringLiteral("path_length"), static_cast<int>(incomingText.pathLength));
    metadata.insert(QStringLiteral("path_hash_mode"), incomingText.pathHashMode);
    metadata.insert(QStringLiteral("path_length_hops"), incomingText.pathLengthHops);
    metadata.insert(QStringLiteral("text_type"), static_cast<int>(incomingText.textType));

    if (incomingText.hasSnr)
        metadata.insert(QStringLiteral("snr_quarter_db"), static_cast<int>(incomingText.snrQuarterDb));
    message.setMetadata(metadata);

    bool inserted = false;
    if (!persistMessage(message, inserted)) {
        deferredMessages.append(message);
        messagesWaiting = true;
        onTransportError(QStringLiteral("Could not persist queued MeshCore message"));
        return;
    }
    if (inserted)
        emit messageReceived(message);
    requestNextMessage();
}

void MeshCoreProtocol::onTransportDisconnected()
{
    if (protocolState == Connected || protocolState == Connecting) {
        rosterSyncTimeoutTimer.stop();
        rosterSyncRetryTimer.stop();
        rosterSyncActive = false;
        if (pendingManagementCommand != NoManagementCommand)
            finishManagementCommand(false,
                QStringLiteral("Device disconnected before the command completed"));
        protocolState = Disconnected;
        deviceState = DeviceOffline;
        appStartSent = false;
        awaitingSelfInfo = false;
        abortContactSync();
        abortChannelSync();
        abortMessageSync();
        messagesWaiting = true;
        currentPublicKeyHex.clear();
        emit disconnected();
        emit deviceStateChanged(deviceState);
    }
}

void MeshCoreProtocol::handleRxLog(const QByteArray &payload)
{
    MeshCoreRxLogEntry rxLog;
    if (!MeshCoreCompanionCodec::decodeRxLogFrame(toBytes(payload), rxLog) ||
        !rxLog.hasPacketHeader || rxLog.payloadType != 0x05 ||
        (rxLog.routeType != 0 && rxLog.routeType != 1))
        return;

    for (const MeshCoreChannel &channel : channels) {
        if (channel.secret.size() != 16)
            continue;
        MeshCoreChannelText text;
        if (!MeshCoreCompanionCodec::decodeChannelTextPayload(
                rxLog.payload, toBytes(channel.secret), text) || text.flags != 0)
            continue;

        const QByteArray echoedText = QByteArray::fromStdString(text.text);
        const QString correlationId = outgoingChannelCorrelationKey(
            static_cast<quint8>(channel.id.toUInt()), text.timestamp, echoedText);
        const QByteArray statusCorrelation = QByteArray::number(channel.id.toUInt()) +
            QChar('\n').toLatin1() + QByteArray::number(text.timestamp) +
            QChar('\n').toLatin1() + echoedText;
        const QString statusKey = QString::fromLatin1(
            QCryptographicHash::hash(statusCorrelation, QCryptographicHash::Sha256).toHex());
        const QString messageId = outgoingChannelCorrelations.value(correlationId);
        QString matchedMessageId = messageId;
        if (matchedMessageId.isEmpty())
            matchedMessageId = outgoingChannelStatusKeys.key(statusKey);
        if (matchedMessageId.isEmpty())
            continue;

        const int hashBytes = qMax(1, rxLog.pathHashBytes);
        if (rxLog.pathHops <= 0)
            continue;
        for (int offset = 0; offset + hashBytes <= static_cast<int>(rxLog.path.size());
             offset += hashBytes) {
            const QByteArray hash(reinterpret_cast<const char *>(rxLog.path.data() + offset),
                                  hashBytes);
            outgoingChannelRepeaters[matchedMessageId].insert(QString::fromLatin1(hash.toHex()));
        }
        const int repeaterCount = outgoingChannelRepeaters.value(matchedMessageId).size();
        const int repeatCount = outgoingChannelRepeatCounts.value(matchedMessageId, 0);
        updateOutgoingMessageStatus(matchedMessageId, repeaterCount, repeatCount);
        if (repeaterCount > 0 &&
            outgoingChannelRetryTimers.contains(matchedMessageId))
            outgoingChannelRetryTimers.value(matchedMessageId)->stop();
        break;
    }
}

void MeshCoreProtocol::updateOutgoingMessageStatus(const QString &messageId,
                                                   int repeaterCount, int repeatCount)
{
    auto messageIt = outgoingChannelMessages.find(messageId);
    if (messageIt == outgoingChannelMessages.end())
        return;
    BasicMessage message = messageIt.value();
    QVariantMap metadata = message.metadata();
    metadata.insert(QStringLiteral("delivery_send_accepted"), true);
    metadata.insert(QStringLiteral("delivery_status_text"), repeaterCount > 0
        ? repeaterCountLabel(repeaterCount)
        : (repeatCount > 0 ? QStringLiteral("Repeat %1").arg(repeatCount)
                           : repeaterCountLabel(0)));
    message.setMetadata(metadata);
    messageIt.value() = message;
    bool inserted = false;
    persistMessage(message, inserted);
    emit messageReceived(message);
}

void MeshCoreProtocol::retryOutgoingMessage(const QString &messageId)
{
    if (protocolState != Connected || !transport ||
        outgoingChannelRepeaters.value(messageId).size() > 0 ||
        !outgoingChannelMessages.contains(messageId))
        return;
    int repeatCount = outgoingChannelRepeatCounts.value(messageId, 0);
    if (repeatCount >= 3) {
        if (QTimer *timer = outgoingChannelRetryTimers.take(messageId))
            timer->deleteLater();
        return;
    }
    const QByteArray wireText = outgoingChannelWireTexts.value(messageId);
    const quint32 timestamp = outgoingChannelTimestamps.value(messageId);
    const quint8 channelIndex = outgoingChannelIndices.value(messageId);
    const auto channel = std::find_if(channels.cbegin(), channels.cend(),
        [channelIndex](const MeshCoreChannel &candidate) {
            return candidate.id.toUInt() == channelIndex;
        });
    if (channel != channels.cend()) {
        std::vector<std::uint8_t> retryPayload;
        const QByteArray messageBody = outgoingChannelBodies.value(messageId);
        const std::string plainText(messageBody.constData(),
                                    static_cast<std::size_t>(messageBody.size()));
        if (MeshCoreCompanionCodec::encodeChannelTextPayload(
                toBytes(channel->secret), timestamp,
                std::string(wireText.constData(), static_cast<std::size_t>(wireText.size())),
                static_cast<std::uint8_t>(repeatCount + 1), retryPayload)) {
            QByteArray retryPacket;
            retryPacket.append(static_cast<char>(0x15));
            retryPacket.append(static_cast<char>(0));
            retryPacket.append(reinterpret_cast<const char *>(retryPayload.data()),
                               static_cast<int>(retryPayload.size()));
            const QByteArray retryCommand = encodeRawPacketCommand(retryPacket, 0);
            if (retryCommand.isEmpty() || !transport->sendPayload(retryCommand))
                return;
            outgoingChannelPackets.insert(messageId, retryPacket);
            outgoingChannelCommands.insert(messageId, retryCommand);
            const QByteArray statusCorrelation = QByteArray::number(channelIndex) + QChar('\n').toLatin1() +
                QByteArray::number(timestamp) + QChar('\n').toLatin1() + messageBody;
            outgoingChannelStatusKeys.insert(messageId, QString::fromLatin1(
                QCryptographicHash::hash(statusCorrelation, QCryptographicHash::Sha256).toHex()));
        } else {
            return;
        }
    } else {
        return;
    }
    ++repeatCount;
    outgoingChannelRepeatCounts.insert(messageId, repeatCount);
    updateOutgoingMessageStatus(messageId, 0, repeatCount);
    QTimer *timer = outgoingChannelRetryTimers.value(messageId, nullptr);
    if (!timer) {
        timer = new QTimer(this);
        timer->setSingleShot(true);
        outgoingChannelRetryTimers.insert(messageId, timer);
        connect(timer, &QTimer::timeout, this,
                [this, messageId]() { retryOutgoingMessage(messageId); });
    }
    timer->start(120000);
}

void MeshCoreProtocol::onPacketReceived(const QByteArray &payload)
{
    if (rosterSyncActive && !payload.isEmpty())
        qWarning() << "[MC-SYNC] RX packet type"
                   << QStringLiteral("0x%1").arg(static_cast<quint8>(payload.at(0)),
                                                 2, 16, QLatin1Char('0'))
                   << "bytes" << payload.size();
    handlePacket(payload);
}

void MeshCoreProtocol::onTransportError(const QString &message)
{
    qWarning() << "MeshCore transport error:" << message;
    
    if (protocolState == Connecting || protocolState == Connected) {
        // Attempt graceful disconnect on error
        disconnect();
    }
    
    deviceState = DeviceError;
    emit deviceStateChanged(deviceState);
}

QList<MeshCoreContact> MeshCoreProtocol::discoverContacts() const { return contacts; }
QList<MeshCoreChannel> MeshCoreProtocol::discoverChannels() const { return channels; }
QList<int> MeshCoreProtocol::availableChannelSlots() const { return freeChannelSlots; }

bool MeshCoreProtocol::persistAndSendOutgoingMessage(
    const BasicMessage &message, const QByteArray &command, const QString &queueError)
{
    bool inserted = false;
    if (!persistMessage(message, inserted) || !inserted)
        return false;

    if (!transport->sendPayload(command)) {
        removePersistedMessage(message);
        onTransportError(queueError);
        return false;
    }

    emit messageReceived(message);
    return true;
}

QByteArray MeshCoreProtocol::encodeRawPacketCommand(const QByteArray &packet,
                                                    quint8 priority) const
{
    if (packet.size() < 3 || packet.size() > 255)
        return QByteArray();
    QByteArray command;
    command.reserve(packet.size() + 2);
    command.append(static_cast<char>(0x41)); // CMD_SEND_RAW_PACKET
    command.append(static_cast<char>(priority));
    command.append(packet);
    return command;
}

bool MeshCoreProtocol::sendMessage(const QString &recipient, const QString &message)
{
    if (protocolState != Connected || !transport || recipient.isEmpty() || message.isEmpty())
        return false;

    const auto contact = std::find_if(contacts.cbegin(), contacts.cend(),
        [&recipient](const MeshCoreContact &candidate) {
            return candidate.id.compare(recipient, Qt::CaseInsensitive) == 0;
        });
    if (contact == contacts.cend())
        return false;

    const QByteArray publicKey = QByteArray::fromHex(contact->id.toLatin1());
    if (publicKey.size() != 32 || publicKey.toHex() != contact->id.toLatin1().toLower())
        return false;

    std::vector<std::uint8_t> destination;
    destination.reserve(6);
    for (int i = 0; i < 6; ++i)
        destination.push_back(static_cast<std::uint8_t>(publicKey.at(i)));

    const QByteArray text = message.toUtf8();
    const std::uint32_t sentAt = static_cast<std::uint32_t>(QDateTime::currentSecsSinceEpoch());
    const std::vector<std::uint8_t> encoded = MeshCoreCompanionCodec::encodeDirectTextCommand(
        0, sentAt, destination, std::string(text.constData(), static_cast<std::size_t>(text.size())));
    if (encoded.empty())
        return false;

    const QDateTime timestamp = QDateTime::fromSecsSinceEpoch(sentAt).toUTC();
    const QString transactionId = QStringLiteral("meshcore-") +
        QUuid::createUuid().toString(QUuid::WithoutBraces);
    BasicMessage localEcho(transactionId, contact->id, QString(), contact->id, message,
                           timestamp, QStringLiteral("meshcore"), BasicMessage::Outgoing);
    QVariantMap metadata;
    metadata.insert(QStringLiteral("txn_id"), transactionId);
    metadata.insert(QStringLiteral("delivery_status"), QStringLiteral("sending"));
    localEcho.setMetadata(metadata);

    const QByteArray command(reinterpret_cast<const char *>(encoded.data()),
                             static_cast<int>(encoded.size()));
    return persistAndSendOutgoingMessage(
        localEcho, command, QStringLiteral("Failed to queue direct MeshCore message"));
}

bool MeshCoreProtocol::sendBroadcastMessage(const QString &message)
{
    return sendMessage(QString(), message);
}

bool MeshCoreProtocol::sendGroupMessage(const QString &channelName, const QString &message)
{
#if !HAVE_MESHCORE_OPENSSL
    Q_UNUSED(channelName);
    Q_UNUSED(message);
    return false;
#else
    if (protocolState != Connected || !transport || channelName.isEmpty() || message.isEmpty())
        return false;

    const QString requestedChannelId = channelName.startsWith(QStringLiteral("channel:"))
        ? channelName.mid(QStringLiteral("channel:").size()) : QString();
    const MeshCoreChannel *selectedChannel = nullptr;
    for (const MeshCoreChannel &channel : channels) {
        if (channel.id == requestedChannelId || channel.name == channelName) {
            if (selectedChannel)
                return false;
            selectedChannel = &channel;
        }
    }
    if (!selectedChannel)
        return false;

    bool indexOk = false;
    const uint channelIndex = selectedChannel->id.toUInt(&indexOk);
    if (!indexOk || channelIndex >= 8)
        return false;

    const QByteArray wireText = localNodeName.isEmpty()
        ? QByteArray() : (localNodeName + QStringLiteral(": ") + message).toUtf8();
    const QByteArray plainText = message.toUtf8();
    if (plainText.size() > 133 || (wireText.isEmpty() ? plainText.size() : wireText.size()) > 155)
        return false;
    const std::uint32_t sentAt = static_cast<std::uint32_t>(QDateTime::currentSecsSinceEpoch());
    const std::vector<std::uint8_t> encoded = MeshCoreCompanionCodec::encodeChannelTextCommand(
        static_cast<std::uint8_t>(channelIndex), sentAt,
        std::string(plainText.constData(), static_cast<std::size_t>(plainText.size())));
    if (encoded.empty())
        return false;

    const QString conversationId = QStringLiteral("channel:") + selectedChannel->id;
    const QDateTime timestamp = QDateTime::fromSecsSinceEpoch(sentAt).toUTC();
    const QString transactionId = QStringLiteral("meshcore-") +
        QUuid::createUuid().toString(QUuid::WithoutBraces);
    BasicMessage localEcho(transactionId, conversationId, QString(), QString(), message,
                           timestamp, QStringLiteral("meshcore"), BasicMessage::Outgoing);
    QVariantMap metadata;
    metadata.insert(QStringLiteral("txn_id"), transactionId);
    metadata.insert(QStringLiteral("delivery_status"), QStringLiteral("sending"));
    localEcho.setMetadata(metadata);

    const QByteArray key = selectedChannel->secret;
    if (key.size() != 16)
        return false;
    const QByteArray encodedText = wireText.isEmpty() ? plainText : wireText;
    const std::string outgoingWireText(encodedText.constData(),
        static_cast<std::size_t>(encodedText.size()));
    std::vector<std::uint8_t> grpPayload;
    if (!MeshCoreCompanionCodec::encodeChannelTextPayload(
            toBytes(key), sentAt, outgoingWireText, 0, grpPayload))
        return false;
    QByteArray rawPacket;
    rawPacket.append(static_cast<char>(0x15)); // PAYLOAD_TYPE_GRP_TXT, ROUTE_TYPE_FLOOD
    rawPacket.append(static_cast<char>(0));
    rawPacket.append(reinterpret_cast<const char *>(grpPayload.data()),
                      static_cast<int>(grpPayload.size()));
    const QByteArray command = encodeRawPacketCommand(rawPacket, 0);
    if (command.isEmpty())
        return false;

    QVariantMap deliveryMetadata = localEcho.metadata();
    deliveryMetadata.insert(QStringLiteral("delivery_send_accepted"), true);
    constexpr int companionRxLogMaximumRawBytes = 173;
    constexpr int maximumMeshPathBytes = 64;
    const bool repeaterEchoObservable = rawPacket.size() + maximumMeshPathBytes <=
        companionRxLogMaximumRawBytes;
    deliveryMetadata.insert(QStringLiteral("delivery_status_text"), repeaterEchoObservable
        ? QStringLiteral("0 Repeaters")
        : QStringLiteral("Repeater-Status nicht verfügbar"));
    localEcho.setMetadata(deliveryMetadata);
    bool inserted = false;
    if (!persistMessage(localEcho, inserted) || !inserted)
        return false;
    if (!transport->sendPayload(command)) {
        removePersistedMessage(localEcho);
        onTransportError(QStringLiteral("Failed to queue MeshCore room message"));
        return false;
    }

    const QByteArray correlationText = wireText.isEmpty() ? plainText : wireText;
    const QString correlationId = outgoingChannelCorrelationKey(
        static_cast<quint8>(channelIndex), sentAt, correlationText);
    const QByteArray statusCorrelation = QByteArray::number(channelIndex) + QChar('\n').toLatin1() +
        QByteArray::number(sentAt) + QChar('\n').toLatin1() + plainText;
    const QString statusKey = QString::fromLatin1(
        QCryptographicHash::hash(statusCorrelation, QCryptographicHash::Sha256).toHex());
    outgoingChannelPackets.insert(transactionId, rawPacket);
    outgoingChannelWireTexts.insert(transactionId, correlationText);
    outgoingChannelBodies.insert(transactionId, plainText);
    outgoingChannelTimestamps.insert(transactionId, sentAt);
    outgoingChannelIndices.insert(transactionId, static_cast<quint8>(channelIndex));
    outgoingChannelStatusKeys.insert(transactionId, statusKey);
    outgoingChannelRepeaters.insert(transactionId, QSet<QString>());
    outgoingChannelRepeatCounts.insert(transactionId, 0);
    outgoingChannelMessages.insert(transactionId, localEcho);
    outgoingChannelCommands.insert(transactionId, command);
    outgoingChannelCorrelations.insert(correlationId, transactionId);
    if (repeaterEchoObservable) {
        QTimer *retryTimer = new QTimer(this);
        retryTimer->setSingleShot(true);
        retryTimer->setProperty("meshcoreMessageId", transactionId);
        outgoingChannelRetryTimers.insert(transactionId, retryTimer);
        connect(retryTimer, &QTimer::timeout, this,
                [this, transactionId]() { retryOutgoingMessage(transactionId); });
        retryTimer->start(120000);
    }
    emit messageReceived(localEcho);
    return true;
#endif
}
