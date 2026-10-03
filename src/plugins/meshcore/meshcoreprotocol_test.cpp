#include "meshcore.h"
#include "meshcorecodec.h"
#include "meshcoreplugin.h"
#include "meshcoretransport.h"
#include "meshcoreblelifecycle.h"

#include <interfaces/iprotocolmessaging.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <functional>
#include <iostream>

class FakeTransport : public MeshCoreTransport
{
public:
    bool open(const QString &endpoint) override
    {
        openCalled = true;
        openedEndpoint = endpoint;
        if (!asynchronousOpen)
            completeConnection();
        return true;
    }

    void completeConnection()
    {
        connectedState = true;
        emit connected();
    }

    void close() override
    {
        connectedState = false;
    }

    bool isConnected() const override
    {
        return connectedState;
    }

    bool sendPayload(const QByteArray &payload) override
    {
        sentPayloads.append(payload);
        if (onSend)
            onSend(payload);
        return true;
    }

    void receivePacket(const QByteArray &payload)
    {
        emit packetReceived(payload);
    }

    bool openCalled = false;
    bool asynchronousOpen = false;
    bool connectedState = false;
    QString openedEndpoint;
    QList<QByteArray> sentPayloads;
    std::function<void(const QByteArray &)> onSend;
};

class TestMeshCoreProtocol : public MeshCoreProtocol
{
public:
    explicit TestMeshCoreProtocol(FakeTransport *transport)
        : fakeTransport(transport)
    {
    }

    mutable QString selectedBackend;

protected:
    MeshCoreTransport *createTransport(const QString &backend) const override
    {
        selectedBackend = backend;
        return fakeTransport;
    }

private:
    FakeTransport *fakeTransport;
};

namespace
{
void appendLe32(QByteArray &data, quint32 value)
{
    data.append(static_cast<char>(value & 0xff));
    data.append(static_cast<char>((value >> 8) & 0xff));
    data.append(static_cast<char>((value >> 16) & 0xff));
    data.append(static_cast<char>((value >> 24) & 0xff));
}

void appendMessageIdentityPart(QByteArray &identity, const QByteArray &part)
{
    identity.append(QByteArray::number(part.size()));
    identity.append(':');
    identity.append(part);
}

QString expectedChannelMessageId(const QString &conversationId, quint32 timestamp,
                                 const QString &rawText)
{
    QByteArray identity;
    appendMessageIdentityPart(identity, QByteArrayLiteral("channel"));
    appendMessageIdentityPart(identity, conversationId.toUtf8());
    appendMessageIdentityPart(identity, QByteArray());
    appendMessageIdentityPart(identity, QByteArray::number(timestamp));
    appendMessageIdentityPart(identity, rawText.toUtf8());
    return QStringLiteral("meshcore-") + QString::fromLatin1(
        QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
}

QByteArray contactRecord(const QByteArray &name, quint32 lastAdvert,
                         const QByteArray &publicKey = QByteArray(), quint8 type = 0x02)
{
    QByteArray record;
    record.append(static_cast<char>(0x03));
    if (publicKey.size() == 32) {
        record.append(publicKey);
    } else {
        for (int i = 0; i < 32; ++i)
            record.append(static_cast<char>(i));
    }
    record.append(static_cast<char>(type));
    record.append(static_cast<char>(0x00)); // flags
    record.append(static_cast<char>(0xff)); // flood path
    record.append(QByteArray(64, '\0'));
    record.append(name.left(32));
    record.append(QByteArray(32 - qMin(name.size(), 32), '\0'));
    appendLe32(record, lastAdvert);
    appendLe32(record, 0); // latitude
    appendLe32(record, 0); // longitude
    appendLe32(record, 1); // last modified
    return record;
}

QByteArray channelInfoPacket(quint8 index, const QByteArray &name)
{
    QByteArray packet(50, '\0');
    packet[0] = static_cast<char>(0x12);
    packet[1] = static_cast<char>(index);
    const QByteArray encodedName = name.left(32);
    for (int i = 0; i < encodedName.size(); ++i)
        packet[2 + i] = encodedName.at(i);
    for (int i = 0; i < 16; ++i)
        packet[34 + i] = static_cast<char>(0xa0 + i); // Must never be exposed as room data.
    return packet;
}

int storedMessageCount(const QString &historyDirectory)
{
    int count = 0;
    QDirIterator files(historyDirectory, {QStringLiteral("*.json")}, QDir::Files,
                       QDirIterator::Subdirectories);
    while (files.hasNext()) {
        files.next();
        ++count;
    }
    return count;
}

bool storedMessageBodyExists(const QString &historyDirectory, const QString &body)
{
    QDirIterator files(historyDirectory, {QStringLiteral("*.json")}, QDir::Files,
                       QDirIterator::Subdirectories);
    while (files.hasNext()) {
        QFile file(files.next());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        if (document.object().value(QStringLiteral("body")).toString() == body)
            return true;
    }
    return false;
}

bool writeLegacyChannelMessage(const QString &historyDirectory, const QString &messageId,
                               const QString &body, const QDateTime &timestamp)
{
    const QString conversationId = QStringLiteral("channel:0");
    const QString conversationDirectory = QDir(historyDirectory).filePath(QString::fromLatin1(
        QCryptographicHash::hash(conversationId.toUtf8(), QCryptographicHash::Sha256).toHex()));
    if (!QDir().mkpath(conversationDirectory))
        return false;

    QJsonObject object;
    object.insert(QStringLiteral("message_id"), messageId);
    object.insert(QStringLiteral("conversation_id"), conversationId);
    object.insert(QStringLiteral("sender"), QString());
    object.insert(QStringLiteral("recipient"), QString());
    object.insert(QStringLiteral("body"), body);
    object.insert(QStringLiteral("timestamp"), timestamp.toUTC().toString(Qt::ISODateWithMs));
    object.insert(QStringLiteral("protocol"), QStringLiteral("meshcore"));
    object.insert(QStringLiteral("direction"), static_cast<int>(BasicMessage::Incoming));

    QFile file(QDir(conversationDirectory).filePath(messageId + QStringLiteral(".json")));
    if (!file.open(QIODevice::WriteOnly))
        return false;
    const QByteArray contents = QJsonDocument(object).toJson(QJsonDocument::Compact);
    return file.write(contents) == contents.size();
}

bool testBleLifecyclePolicy()
{
    MeshCoreBleLifecycle lifecycle;
    bool passed = true;
    const auto expect = [&passed](bool condition, const char *message) {
        if (!condition) {
            std::cerr << "BLE lifecycle policy: " << message << '\n';
            passed = false;
        }
    };

    expect(lifecycle.requestOpen() == MeshCoreBleLifecycle::OpenDisposition::Start,
           "idle open should start a session");
    expect(lifecycle.state() == MeshCoreBleLifecycle::State::Opening,
           "session should enter Opening");
    expect(lifecycle.requestOpen() == MeshCoreBleLifecycle::OpenDisposition::AlreadyActive,
           "repeated open while opening should be idempotent");

    lifecycle.markConnected();
    expect(lifecycle.state() == MeshCoreBleLifecycle::State::Connected,
           "connected callback should advance session state");
    expect(lifecycle.requestOpen() == MeshCoreBleLifecycle::OpenDisposition::AlreadyActive,
           "repeated open while connected should be idempotent");

    lifecycle.beginClose();
    expect(lifecycle.state() == MeshCoreBleLifecycle::State::Closing,
           "close should retain Closing until controller disconnects");
    expect(lifecycle.requestOpen() == MeshCoreBleLifecycle::OpenDisposition::Deferred &&
               lifecycle.hasPendingOpen(),
           "open during Closing should be deferred");
    expect(!MeshCoreBleLifecycle::controllerMayBeDestroyed(false) &&
               MeshCoreBleLifecycle::controllerMayBeDestroyed(true),
           "controller may be destroyed only after UnconnectedState");
    expect(lifecycle.controllerDestroyed(), "deferred open should resume after close completion");
    expect(lifecycle.state() == MeshCoreBleLifecycle::State::Idle &&
               !lifecycle.hasPendingOpen(),
           "close completion should consume the deferred open request");

    lifecycle.requestOpen();
    lifecycle.beginClose();
    lifecycle.requestOpen();
    lifecycle.cancelPendingOpen();
    expect(!lifecycle.controllerDestroyed(), "explicit close should cancel deferred reopen");
    expect(lifecycle.state() == MeshCoreBleLifecycle::State::Idle,
           "canceled close should finish in Idle");
    return passed;
}
}

int main(int argc, char *argv[])
{
    QTemporaryDir dataHome;
    if (!dataHome.isValid()) {
        std::cerr << "failed to create isolated message-history directory\n";
        return 1;
    }
    qputenv("XDG_DATA_HOME", dataHome.path().toUtf8());
    QCoreApplication application(argc, argv);
    application.setOrganizationName(QStringLiteral("VacuumTests"));
    application.setApplicationName(QStringLiteral("meshcore_protocol_tests"));
    if (!testBleLifecyclePolicy())
        return 1;
    const QString historyDirectory = QDir(QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation)).filePath(QStringLiteral("meshcore/messages"));
    MeshCorePlugin pluginProvider;
    IProtocolMessaging *messaging = qobject_cast<IProtocolMessaging *>(pluginProvider.instance());
    if (!messaging) {
        std::cerr << "MeshCore plugin does not expose IProtocolMessaging\n";
        return 1;
    }
    const ConversationId directConversationId =
        QStringLiteral("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
    const Jid directAddress = messaging->addressForConversation(directConversationId);
    ConversationId mappedConversationId;
    if (!directAddress.isValid() || directAddress.domain() != QStringLiteral("meshcore.local") ||
        !messaging->conversationIdForAddress(directAddress, mappedConversationId) ||
        mappedConversationId != directConversationId) {
        std::cerr << "direct conversation address did not round-trip through the adapter\n";
        return 1;
    }
    const ConversationId roomConversationId = QStringLiteral("channel:0");
    const Jid roomAddress = messaging->addressForConversation(roomConversationId);
    mappedConversationId.clear();
    if (!roomAddress.isValid() || roomAddress.domain() != QStringLiteral("meshcore.local") ||
        !messaging->conversationIdForAddress(roomAddress, mappedConversationId) ||
        mappedConversationId != roomConversationId) {
        std::cerr << "room conversation address did not round-trip through the adapter\n";
        return 1;
    }
    FakeTransport *transport = new FakeTransport();

    // Test 1: Default behavior (USB)
    TestMeshCoreProtocol protocol(transport);
    if (!protocol.initialize(QStringLiteral("/dev/meshcore-test"))) {
        std::cerr << "initialize unexpectedly failed\n";
        return 1;
    }
    if (!protocol.connectToDevice()) {
        std::cerr << "connectToDevice unexpectedly failed\n";
        return 1;
    }
    if (protocol.selectedBackend != QStringLiteral("usb")) {
        std::cerr << "default transport was not USB\n";
        return 1;
    }
    if (!transport->openCalled) {
        std::cerr << "transport factory result was not opened\n";
        return 1;
    }
    if (transport->openedEndpoint != QStringLiteral("/dev/meshcore-test")) {
        std::cerr << "USB endpoint was not passed through: got '"
                  << transport->openedEndpoint.toStdString() << "'\n";
        return 1;
    }

    if (transport->sentPayloads.size() != 1 ||
        transport->sentPayloads.constFirst() != QByteArray::fromHex("0100000000000000")) {
        std::cerr << "connection did not send the Companion APP_START command\n";
        return 1;
    }

    int contactsChangedCount = 0;
    int channelsChangedCount = 0;
    int messagesReceivedCount = 0;
    QString lastMessageSender;
    QString lastMessageBody;
    BasicMessage lastReceivedMessage;
    QObject::connect(&protocol, &MeshCoreProtocol::contactsChanged,
                     &application, [&contactsChangedCount]() { ++contactsChangedCount; });
    QObject::connect(&protocol, &MeshCoreProtocol::channelsChanged,
                     &application, [&channelsChangedCount]() { ++channelsChangedCount; });
    QObject::connect(&protocol, &MeshCoreProtocol::messageReceived, &application,
                     [&messagesReceivedCount, &lastMessageSender, &lastMessageBody,
                      &lastReceivedMessage](const BasicMessage &message) {
        ++messagesReceivedCount;
        lastMessageSender = message.sender();
        lastMessageBody = message.body();
        lastReceivedMessage = message;
    });
    bool persistedBeforeQueueAdvance = true;
    transport->onSend = [&](const QByteArray &payload) {
        if (payload == QByteArray(1, static_cast<char>(0x0a)) && messagesReceivedCount > 0 &&
            storedMessageCount(historyDirectory) < messagesReceivedCount)
            persistedBeforeQueueAdvance = false;
    };


    QByteArray incomingDirectMessage;
    incomingDirectMessage.append(static_cast<char>(0x07));
    incomingDirectMessage.append(QByteArray::fromHex("000102030405"));
    incomingDirectMessage.append(static_cast<char>(0xff));
    incomingDirectMessage.append(static_cast<char>(0x00));
    appendLe32(incomingDirectMessage, 2345);
    incomingDirectMessage.append("Hello from mesh");

    QByteArray incomingChannelMessage;
    incomingChannelMessage.append(static_cast<char>(0x08));
    incomingChannelMessage.append(static_cast<char>(0));
    incomingChannelMessage.append(static_cast<char>(0xff));
    incomingChannelMessage.append(static_cast<char>(0x00));
    appendLe32(incomingChannelMessage, 3456);
    incomingChannelMessage.append("Hello room");

    // Test: Channel message with sender prefix
    QByteArray incomingChannelMessageWithPrefix;
    incomingChannelMessageWithPrefix.append(static_cast<char>(0x08));
    incomingChannelMessageWithPrefix.append(static_cast<char>(0));
    incomingChannelMessageWithPrefix.append(static_cast<char>(0xff));
    incomingChannelMessageWithPrefix.append(static_cast<char>(0x00));
    appendLe32(incomingChannelMessageWithPrefix, 3457);
    incomingChannelMessageWithPrefix.append(QStringLiteral("🌻Alice: Hello room").toUtf8());

    QByteArray incomingChannelData(10, '\0');
    incomingChannelData[0] = static_cast<char>(0x1b);
    incomingChannelData[1] = static_cast<char>(0);
    incomingChannelData[2] = static_cast<char>(0xff);
    incomingChannelData[3] = static_cast<char>(0);
    incomingChannelData[4] = static_cast<char>(0);
    incomingChannelData[5] = static_cast<char>(0);
    incomingChannelData[6] = static_cast<char>(0);
    incomingChannelData[7] = static_cast<char>(0);
    incomingChannelData[8] = static_cast<char>(0x01);
    incomingChannelData[9] = static_cast<char>(0x02);

    QByteArray selfInfo(58, '\0');
    selfInfo[0] = static_cast<char>(0x05);
    transport->receivePacket(selfInfo);
    if (transport->sentPayloads.size() != 2 ||
        transport->sentPayloads.constLast() != QByteArray(1, static_cast<char>(0x04))) {
        std::cerr << "SELF_INFO did not trigger a full contacts request\n";
        return 1;
    }

    QByteArray contactStart(1, static_cast<char>(0x02));
    appendLe32(contactStart, 1);
    transport->receivePacket(contactStart);
    transport->receivePacket(contactRecord(QByteArray("Alice"), 1234));
    QByteArray contactEnd(1, static_cast<char>(0x04));
    appendLe32(contactEnd, 1234);
    transport->receivePacket(contactEnd);

    const QList<MeshCoreContact> contacts = protocol.discoverContacts();
    if (contactsChangedCount != 1 || contacts.size() != 1 ||
        contacts.first().id != QStringLiteral("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f") ||
        contacts.first().name != QStringLiteral("Alice") || contacts.first().lastSeen != 1234) {
        std::cerr << "valid contact list was not published correctly\n";
        return 1;
    }

    if (transport->sentPayloads.size() != 3 ||
        transport->sentPayloads.constLast() != QByteArray::fromHex("1f00")) {
        std::cerr << "completed contact sync did not request channel zero\n";
        return 1;
    }

    for (quint8 index = 0; index < 8; ++index) {
        const QByteArray name = index == 0 ? QByteArray("Operations") : QByteArray();
        transport->receivePacket(channelInfoPacket(index, name));
        if (index < 7) {
            const QByteArray request = transport->sentPayloads.constLast();
            if (request.size() != 2 || request.at(0) != static_cast<char>(0x1f) ||
                request.at(1) != static_cast<char>(index + 1)) {
                std::cerr << "channel scan did not request indices sequentially\n";
                return 1;
            }
        }
    }

    if (transport->sentPayloads.constLast() != QByteArray(1, static_cast<char>(0x0a))) {
        std::cerr << "completed channel sync did not start queued-message polling\n";
        return 1;
    }
    transport->receivePacket(incomingDirectMessage);
    if (messagesReceivedCount != 1 || lastMessageBody != QStringLiteral("Hello from mesh") ||
        !lastMessageSender.startsWith(QStringLiteral("000102030405")) ||
        lastReceivedMessage.conversationId() != QStringLiteral("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f") ||
        lastReceivedMessage.timestamp().toSecsSinceEpoch() != 2345 ||
        lastReceivedMessage.protocol() != QStringLiteral("meshcore") ||
        lastReceivedMessage.direction() != BasicMessage::Incoming ||
        storedMessageCount(historyDirectory) != 1 ||
        !storedMessageBodyExists(historyDirectory, QStringLiteral("Hello from mesh")) ||
        transport->sentPayloads.constLast() != QByteArray(1, static_cast<char>(0x0a))) {
        std::cerr << "direct queued message was not emitted, persisted, and followed by another poll\n";
        return 1;
    }
    MeshCoreProtocol historyReader;
    const QList<BasicMessage> reloadedDirectHistory =
        historyReader.conversationHistory(lastReceivedMessage.conversationId());
    if (reloadedDirectHistory.size() != 1 || reloadedDirectHistory.constFirst() != lastReceivedMessage) {
        std::cerr << "persisted direct message history did not round-trip\n";
        return 1;
    }
    transport->receivePacket(incomingChannelData);
    if (messagesReceivedCount != 1 ||
        transport->sentPayloads.constLast() != QByteArray(1, static_cast<char>(0x0a))) {
        std::cerr << "binary channel data stopped the queued-message drain\n";
        return 1;
    }
    transport->receivePacket(incomingChannelMessage);
    if (messagesReceivedCount != 2 || lastMessageBody != QStringLiteral("Hello room") ||
        lastReceivedMessage.conversationId() != QStringLiteral("channel:0") ||
        lastReceivedMessage.timestamp().toSecsSinceEpoch() != 3456 ||
        lastReceivedMessage.protocol() != QStringLiteral("meshcore") ||
        storedMessageCount(historyDirectory) != 2 ||
        !storedMessageBodyExists(historyDirectory, QStringLiteral("Hello room")) ||
        transport->sentPayloads.constLast() != QByteArray(1, static_cast<char>(0x0a))) {
        std::cerr << "channel queued message was not emitted and followed by another poll\n";
        return 1;
    }

    // Test: Channel message with sender prefix - should split sender from body
    transport->receivePacket(incomingChannelMessageWithPrefix);
    if (messagesReceivedCount != 3 || lastMessageBody != QStringLiteral("Hello room") ||
        lastReceivedMessage.conversationId() != QStringLiteral("channel:0") ||
        lastReceivedMessage.timestamp().toSecsSinceEpoch() != 3457 ||
        lastReceivedMessage.protocol() != QStringLiteral("meshcore") ||
        storedMessageCount(historyDirectory) != 3 ||
        !storedMessageBodyExists(historyDirectory, QStringLiteral("Hello room")) ||
        transport->sentPayloads.constLast() != QByteArray(1, static_cast<char>(0x0a)) ||
        lastMessageSender != QStringLiteral("🌻Alice") ||
        lastReceivedMessage.messageId() != expectedChannelMessageId(
            QStringLiteral("channel:0"), 3457, QStringLiteral("🌻Alice: Hello room"))) {
        std::cerr << "channel queued message with prefix was not properly split\n";
        return 1;
    }

    // Channel names are separated at the protocol delimiter; keep punctuation in the name.
    QByteArray incomingChannelMessageWithColonInName;
    incomingChannelMessageWithColonInName.append(static_cast<char>(0x08));
    incomingChannelMessageWithColonInName.append(static_cast<char>(0));
    incomingChannelMessageWithColonInName.append(static_cast<char>(0xff));
    incomingChannelMessageWithColonInName.append(static_cast<char>(0x00));
    appendLe32(incomingChannelMessageWithColonInName, 3458);
    incomingChannelMessageWithColonInName.append("Alice:Bob: Hello room");

    transport->receivePacket(incomingChannelMessageWithColonInName);
    if (messagesReceivedCount != 4 || lastMessageBody != QStringLiteral("Hello room") ||
        lastReceivedMessage.conversationId() != QStringLiteral("channel:0") ||
        lastReceivedMessage.timestamp().toSecsSinceEpoch() != 3458 ||
        lastReceivedMessage.protocol() != QStringLiteral("meshcore") ||
        storedMessageCount(historyDirectory) != 4 ||
        !storedMessageBodyExists(historyDirectory, QStringLiteral("Hello room")) ||
        transport->sentPayloads.constLast() != QByteArray(1, static_cast<char>(0x0a)) ||
        lastMessageSender != QStringLiteral("Alice:Bob")) {
        std::cerr << "channel queued message with colon in sender name was not properly processed\n";
        return 1;
    }

    // Test: Channel message without prefix - should remain unchanged
    QByteArray incomingChannelMessageWithoutPrefix;
    incomingChannelMessageWithoutPrefix.append(static_cast<char>(0x08));
    incomingChannelMessageWithoutPrefix.append(static_cast<char>(0));
    incomingChannelMessageWithoutPrefix.append(static_cast<char>(0xff));
    incomingChannelMessageWithoutPrefix.append(static_cast<char>(0x00));
    appendLe32(incomingChannelMessageWithoutPrefix, 3459);
    incomingChannelMessageWithoutPrefix.append("Hello room without prefix");

    transport->receivePacket(incomingChannelMessageWithoutPrefix);
    if (messagesReceivedCount != 5 || lastMessageBody != QStringLiteral("Hello room without prefix") ||
        lastReceivedMessage.conversationId() != QStringLiteral("channel:0") ||
        lastReceivedMessage.timestamp().toSecsSinceEpoch() != 3459 ||
        lastReceivedMessage.protocol() != QStringLiteral("meshcore") ||
        storedMessageCount(historyDirectory) != 5 ||
        !storedMessageBodyExists(historyDirectory, QStringLiteral("Hello room without prefix")) ||
        transport->sentPayloads.constLast() != QByteArray(1, static_cast<char>(0x0a)) ||
        lastMessageSender != QString()) {
        std::cerr << "channel queued message without prefix was not handled correctly\n";
        return 1;
    }

    // Test: Empty sender name - should be handled gracefully
    QByteArray incomingChannelMessageWithEmptySender;
    incomingChannelMessageWithEmptySender.append(static_cast<char>(0x08));
    incomingChannelMessageWithEmptySender.append(static_cast<char>(0));
    incomingChannelMessageWithEmptySender.append(static_cast<char>(0xff));
    incomingChannelMessageWithEmptySender.append(static_cast<char>(0x00));
    appendLe32(incomingChannelMessageWithEmptySender, 3460);
    incomingChannelMessageWithEmptySender.append(": Hello room with empty sender");

    transport->receivePacket(incomingChannelMessageWithEmptySender);
    if (messagesReceivedCount != 6 || lastMessageBody != QStringLiteral(": Hello room with empty sender") ||
        lastReceivedMessage.conversationId() != QStringLiteral("channel:0") ||
        lastReceivedMessage.timestamp().toSecsSinceEpoch() != 3460 ||
        lastReceivedMessage.protocol() != QStringLiteral("meshcore") ||
        storedMessageCount(historyDirectory) != 6 ||
        !storedMessageBodyExists(historyDirectory, QStringLiteral(": Hello room with empty sender")) ||
        transport->sentPayloads.constLast() != QByteArray(1, static_cast<char>(0x0a)) ||
        lastMessageSender != QString()) {
        std::cerr << "channel queued message with empty sender was not handled correctly\n";
        return 1;
    }

    const QString legacyBody = QStringLiteral("Bob: older room message");
    const QString legacyMessageId = expectedChannelMessageId(
        QStringLiteral("channel:0"), 3444, legacyBody);
    if (!writeLegacyChannelMessage(historyDirectory, legacyMessageId, legacyBody,
                                   QDateTime::fromSecsSinceEpoch(3444).toUTC())) {
        std::cerr << "could not create legacy channel history fixture\n";
        return 1;
    }
    const QList<BasicMessage> reloadedChannelHistory =
        historyReader.conversationHistory(lastReceivedMessage.conversationId());
    if (reloadedChannelHistory.size() != 6 || reloadedChannelHistory.constLast() != lastReceivedMessage) {
        std::cerr << "persisted channel message history did not round-trip\n";
        return 1;
    }
    BasicMessage normalizedLegacyMessage;
    for (const BasicMessage &message : reloadedChannelHistory)
        if (message.messageId() == legacyMessageId)
            normalizedLegacyMessage = message;
    if (normalizedLegacyMessage.sender() != QStringLiteral("Bob") ||
        normalizedLegacyMessage.body() != QStringLiteral("older room message") ||
        !storedMessageBodyExists(historyDirectory, legacyBody)) {
        std::cerr << "legacy channel history was not split in memory while preserving its file\n";
        return 1;
    }
    const int requestsBeforeQueueEnd = transport->sentPayloads.size();
    transport->receivePacket(QByteArray(1, static_cast<char>(0x0a)));
    if (transport->sentPayloads.size() != requestsBeforeQueueEnd) {
        std::cerr << "empty queued-message response requested another message\n";
        return 1;
    }
    if (!persistedBeforeQueueAdvance) {
        std::cerr << "message was not persisted before the next destructive queue request\n";
        return 1;
    }

    transport->receivePacket(QByteArray(1, static_cast<char>(0x83)));
    if (transport->sentPayloads.constLast() != QByteArray(1, static_cast<char>(0x0a))) {
        std::cerr << "pending-message notification did not restart queue polling\n";
        return 1;
    }
    transport->receivePacket(incomingDirectMessage);
    if (messagesReceivedCount != 6 || storedMessageCount(historyDirectory) != 7 ||
        transport->sentPayloads.constLast() != QByteArray(1, static_cast<char>(0x0a))) {
        std::cerr << "replayed queued message was not suppressed idempotently\n";
        return 1;
    }
    transport->receivePacket(QByteArray(1, static_cast<char>(0x0a)));

    transport->receivePacket(QByteArray(1, static_cast<char>(0x83)));
    QByteArray malformedDirectMessage(1, static_cast<char>(0x07));
    transport->receivePacket(malformedDirectMessage);
    if (messagesReceivedCount != 6 ||
        transport->sentPayloads.constLast() != QByteArray(1, static_cast<char>(0x0a))) {
        std::cerr << "malformed queued text stranded the remaining queue\n";
        return 1;
    }
    transport->receivePacket(QByteArray(1, static_cast<char>(0x0a)));

    const QList<MeshCoreChannel> channels = protocol.discoverChannels();
    if (channelsChangedCount != 1 || channels.size() != 1 ||
        channels.first().id != QStringLiteral("0") ||
        channels.first().name != QStringLiteral("Operations") ||
        !channels.first().topic.isEmpty() || !channels.first().members.isEmpty()) {
        std::cerr << "active channel was not published as a data-minimal room\n";
        return 1;
    }

    const QString configuredChannelName = QStringLiteral("Private Test");
    const QByteArray configuredChannelSecret = QByteArray::fromHex(
        "00112233445566778899aabbccddeeff");
    bool channelConfigurationFinished = false;
    bool channelConfigurationSucceeded = false;
    QObject::connect(&protocol, &MeshCoreProtocol::channelConfigurationFinished,
                     &application, [&](int index, bool success, const QString &error) {
        channelConfigurationFinished = index == 1 && error.isEmpty();
        channelConfigurationSucceeded = success;
    });
    bool channelConfigurationAccepted = false;
    const bool setChannelInvoked = QMetaObject::invokeMethod(
        &protocol, "setChannel", Qt::DirectConnection,
        Q_RETURN_ARG(bool, channelConfigurationAccepted),
        Q_ARG(int, 1), Q_ARG(QString, configuredChannelName),
        Q_ARG(QByteArray, configuredChannelSecret));
    if (!setChannelInvoked || !channelConfigurationAccepted) {
        std::cerr << "MeshCore setChannel command was not accepted\n";
        return 1;
    }
    const std::vector<std::uint8_t> configuredChannelPayload =
        MeshCoreCompanionCodec::encodeSetChannel(
            1, configuredChannelName.toUtf8().toStdString(),
            std::vector<std::uint8_t>(configuredChannelSecret.begin(),
                                      configuredChannelSecret.end()));
    if (transport->sentPayloads.constLast() != QByteArray(
            reinterpret_cast<const char *>(configuredChannelPayload.data()),
            static_cast<int>(configuredChannelPayload.size()))) {
        std::cerr << "setChannel did not send the exact Companion command\n";
        return 1;
    }
    transport->receivePacket(QByteArray(1, static_cast<char>(0x00)));
    if (transport->sentPayloads.constLast() != QByteArray::fromHex("1f00")) {
        std::cerr << "successful SET_CHANNEL did not refresh channel slot zero\n";
        return 1;
    }
    for (quint8 index = 0; index < 8; ++index) {
        const QByteArray name = index == 0 ? QByteArray("Operations") :
            index == 1 ? configuredChannelName.toUtf8() : QByteArray();
        transport->receivePacket(channelInfoPacket(index, name));
    }
    const QList<MeshCoreChannel> configuredChannels = protocol.discoverChannels();
    if (channelsChangedCount != 2 || configuredChannels.size() != 2 ||
        configuredChannels.at(1).id != QStringLiteral("1") ||
        configuredChannels.at(1).name != configuredChannelName ||
        !channelConfigurationFinished || !channelConfigurationSucceeded ||
        transport->sentPayloads.constLast() != QByteArray(1, static_cast<char>(0x0a))) {
        std::cerr << "successful SET_CHANNEL was not reflected after catalog refresh\n";
        return 1;
    }
    transport->receivePacket(QByteArray(1, static_cast<char>(0x0a)));

    const QString addedContactPublicKey = QStringLiteral(
        "202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f");
    const QString addedContactName = QStringLiteral("Bob");
    bool contactAdditionFinished = false;
    bool contactAdditionSucceeded = false;
    QString contactAdditionResultId;
    QObject::connect(&protocol, &MeshCoreProtocol::contactAdditionFinished,
                     &application, [&](const QString &publicKey, bool success,
                                       const QString &error) {
        contactAdditionFinished = error.isEmpty();
        contactAdditionSucceeded = success;
        contactAdditionResultId = publicKey;
    });
    bool contactAdditionAccepted = false;
    const bool addContactInvoked = QMetaObject::invokeMethod(
        &protocol, "addContact", Qt::DirectConnection,
        Q_RETURN_ARG(bool, contactAdditionAccepted),
        Q_ARG(QString, addedContactPublicKey), Q_ARG(QString, addedContactName));
    if (!addContactInvoked || !contactAdditionAccepted) {
        std::cerr << "MeshCore addContact command was not accepted\n";
        return 1;
    }
    const std::vector<std::uint8_t> addContactPayload =
        MeshCoreCompanionCodec::encodeAddUpdateContact(
            addedContactPublicKey.toStdString(), 0x01, 0x00, -1, -1, "",
            addedContactName.toUtf8().toStdString(), 0, 0, 0);
    if (transport->sentPayloads.constLast() != QByteArray(
            reinterpret_cast<const char *>(addContactPayload.data()),
            static_cast<int>(addContactPayload.size()))) {
        std::cerr << "addContact did not send the exact Companion command\n";
        return 1;
    }
    transport->receivePacket(QByteArray(1, static_cast<char>(0x00)));
    if (transport->sentPayloads.constLast() != QByteArray(1, static_cast<char>(0x04))) {
        std::cerr << "successful ADD_UPDATE_CONTACT did not refresh contacts\n";
        return 1;
    }
    QByteArray refreshedContactStart(1, static_cast<char>(0x02));
    appendLe32(refreshedContactStart, 2);
    transport->receivePacket(refreshedContactStart);
    transport->receivePacket(contactRecord(QByteArray("Alice"), 1234));
    transport->receivePacket(contactRecord(QByteArray("Bob"), 2345,
        QByteArray::fromHex(addedContactPublicKey.toLatin1()), 0x01));
    QByteArray refreshedContactEnd(1, static_cast<char>(0x04));
    appendLe32(refreshedContactEnd, 2345);
    transport->receivePacket(refreshedContactEnd);
    if (transport->sentPayloads.constLast() != QByteArray::fromHex("1f00")) {
        std::cerr << "completed contact refresh did not refresh channels\n";
        return 1;
    }
    for (quint8 index = 0; index < 8; ++index) {
        const QByteArray name = index == 0 ? QByteArray("Operations") :
            index == 1 ? configuredChannelName.toUtf8() : QByteArray();
        transport->receivePacket(channelInfoPacket(index, name));
    }
    bool addedContactFound = false;
    for (const MeshCoreContact &contact : protocol.discoverContacts()) {
        if (contact.id == addedContactPublicKey && contact.name == addedContactName)
            addedContactFound = true;
    }
    if (contactsChangedCount != 2 || !addedContactFound ||
        channelsChangedCount != 3 || !contactAdditionFinished ||
        !contactAdditionSucceeded || contactAdditionResultId != addedContactPublicKey ||
        transport->sentPayloads.constLast() != QByteArray(1, static_cast<char>(0x0a))) {
        std::cerr << "successful ADD_UPDATE_CONTACT was not reflected after catalog refresh\n";
        return 1;
    }
    transport->receivePacket(QByteArray(1, static_cast<char>(0x0a)));

    bool channelErrorReported = false;
    QObject::connect(&protocol, &MeshCoreProtocol::channelConfigurationFinished,
                     &application, [&channelErrorReported](int index, bool success,
                                                          const QString &error) {
        if (index == 2)
            channelErrorReported = !success && error.contains(QStringLiteral("7"));
    });
    const int sendsBeforeRejectedChannel = transport->sentPayloads.size();
    const QByteArray rejectedChannelSecret = QByteArray::fromHex(
        "ffeeddccbbaa99887766554433221100");
    const bool rejectedChannelQueued = protocol.setChannel(
        2, QStringLiteral("Rejected"), rejectedChannelSecret);
    const bool overlappingChannelQueued = protocol.setChannel(
        3, QStringLiteral("Must not overlap"), rejectedChannelSecret);
    if (!rejectedChannelQueued || overlappingChannelQueued ||
        transport->sentPayloads.size() != sendsBeforeRejectedChannel + 1) {
        std::cerr << "management command did not serialize outstanding writes\n";
        return 1;
    }
    transport->receivePacket(QByteArray::fromHex("0107"));
    if (!channelErrorReported ||
        transport->sentPayloads.size() != sendsBeforeRejectedChannel + 1) {
        std::cerr << "SET_CHANNEL rejection was not surfaced without starting a refresh\n";
        return 1;
    }

    const QString rejectedContactKey = QStringLiteral(
        "f0e0d0c0b0a090807060504030201000ffeeddccbbaa99887766554433221100");
    const int sendsBeforeRejectedContact = transport->sentPayloads.size();
    if (!protocol.addContact(rejectedContactKey, QStringLiteral("Rejected"))) {
        std::cerr << "valid ADD_UPDATE_CONTACT command was rejected before transport\n";
        return 1;
    }
    transport->receivePacket(QByteArray::fromHex("0105"));
    if (contactAdditionSucceeded || contactAdditionFinished ||
        contactAdditionResultId != rejectedContactKey ||
        transport->sentPayloads.size() != sendsBeforeRejectedContact + 1) {
        std::cerr << "ADD_UPDATE_CONTACT rejection was not surfaced correctly\n";
        return 1;
    }

    const QString outgoingDirectText = QStringLiteral("direct out");
    if (!protocol.sendMessage(contacts.first().id, outgoingDirectText)) {
        std::cerr << "direct message was not accepted by the connected transport\n";
        return 1;
    }
    const QByteArray directCommand = transport->sentPayloads.constLast();
    if (directCommand.size() != 13 + outgoingDirectText.toUtf8().size() ||
        directCommand.left(3) != QByteArray::fromHex("020000") ||
        directCommand.mid(7, 6) != QByteArray::fromHex("000102030405") ||
        directCommand.mid(13) != outgoingDirectText.toUtf8() ||
        lastReceivedMessage.direction() != BasicMessage::Outgoing ||
        lastReceivedMessage.conversationId() != contacts.first().id ||
        lastReceivedMessage.body() != outgoingDirectText) {
        std::cerr << "direct message command or local echo was malformed\n";
        return 1;
    }

    const QString outgoingRoomText = QStringLiteral("room out");
    if (!protocol.sendGroupMessage(QStringLiteral("channel:0"), outgoingRoomText)) {
        std::cerr << "room message was not accepted by the connected transport\n";
        return 1;
    }
    const QByteArray roomCommand = transport->sentPayloads.constLast();
    if (roomCommand.size() != 7 + outgoingRoomText.toUtf8().size() ||
        roomCommand.left(3) != QByteArray::fromHex("030000") ||
        roomCommand.mid(7) != outgoingRoomText.toUtf8() ||
        lastReceivedMessage.direction() != BasicMessage::Outgoing ||
        lastReceivedMessage.conversationId() != QStringLiteral("channel:0") ||
        lastReceivedMessage.body() != outgoingRoomText) {
        std::cerr << "room message command or local echo was malformed\n";
        return 1;
    }

    FakeTransport *interfaceTransport = new FakeTransport();
    TestMeshCoreProtocol *interfaceProtocol = new TestMeshCoreProtocol(interfaceTransport);
    if (!interfaceProtocol->initialize(QStringLiteral("/dev/meshcore-interface-test")) ||
        !interfaceProtocol->connectToDevice()) {
        std::cerr << "interface protocol setup failed\n";
        return 1;
    }
    QByteArray interfaceSelfInfo(58, '\0');
    interfaceSelfInfo[0] = static_cast<char>(0x05);
    interfaceTransport->receivePacket(interfaceSelfInfo);
    QByteArray interfaceContactStart(1, static_cast<char>(0x02));
    appendLe32(interfaceContactStart, 1);
    interfaceTransport->receivePacket(interfaceContactStart);
    interfaceTransport->receivePacket(contactRecord(QByteArray("Alice"), 1234));
    QByteArray interfaceContactEnd(1, static_cast<char>(0x04));
    appendLe32(interfaceContactEnd, 1234);
    interfaceTransport->receivePacket(interfaceContactEnd);
    for (quint8 index = 0; index < 8; ++index)
        interfaceTransport->receivePacket(channelInfoPacket(
            index, index == 0 ? QByteArray("Operations") : QByteArray()));
    interfaceTransport->receivePacket(QByteArray(1, static_cast<char>(0x0a)));

    MeshCorePlugin interfaceProvider(interfaceProtocol);
    IProtocolMessaging *genericMessaging =
        qobject_cast<IProtocolMessaging *>(interfaceProvider.instance());
    if (!genericMessaging) {
        std::cerr << "active MeshCore provider did not expose IProtocolMessaging\n";
        return 1;
    }
    int interfaceMessageCount = 0;
    BasicMessage lastInterfaceMessage;
    QObject::connect(&interfaceProvider, &MeshCorePlugin::protocolMessageReceived,
                     &application, [&interfaceMessageCount, &lastInterfaceMessage](
                         const BasicMessage &message) {
        ++interfaceMessageCount;
        lastInterfaceMessage = message;
    });
    const QString interfaceDirectText = QStringLiteral("interface direct");
    const BasicMessage interfaceDirectMessage(QStringLiteral("ui-direct-1"), contacts.first().id,
        QString(), contacts.first().id, interfaceDirectText, QDateTime::currentDateTimeUtc(),
        QStringLiteral("meshcore"), BasicMessage::Outgoing);
    if (!genericMessaging->sendMessage(interfaceDirectMessage) || interfaceMessageCount != 1 ||
        lastInterfaceMessage.conversationId() != contacts.first().id ||
        lastInterfaceMessage.body() != interfaceDirectText ||
        lastInterfaceMessage.direction() != BasicMessage::Outgoing ||
        interfaceTransport->sentPayloads.constLast().left(3) != QByteArray::fromHex("020000") ||
        interfaceTransport->sentPayloads.constLast().mid(7, 6) != QByteArray::fromHex("000102030405") ||
        interfaceTransport->sentPayloads.constLast().mid(13) != interfaceDirectText.toUtf8()) {
        std::cerr << "direct message did not traverse IProtocolMessaging to the transport\n";
        return 1;
    }
    const QString interfaceRoomText = QStringLiteral("interface room");
    const BasicMessage interfaceRoomMessage(QStringLiteral("ui-room-1"), QStringLiteral("channel:0"),
        QString(), QString(), interfaceRoomText, QDateTime::currentDateTimeUtc(),
        QStringLiteral("meshcore"), BasicMessage::Outgoing);
    if (!genericMessaging->sendMessage(interfaceRoomMessage) || interfaceMessageCount != 2 ||
        lastInterfaceMessage.conversationId() != QStringLiteral("channel:0") ||
        lastInterfaceMessage.body() != interfaceRoomText ||
        interfaceTransport->sentPayloads.constLast().left(3) != QByteArray::fromHex("030000") ||
        interfaceTransport->sentPayloads.constLast().mid(7) != interfaceRoomText.toUtf8() ||
        genericMessaging->conversationDisplayName(QStringLiteral("channel:0")) !=
            QStringLiteral("Operations")) {
        std::cerr << "room message did not traverse IProtocolMessaging to the transport\n";
        return 1;
    }

    // Test 2: Explicit backend selection
    FakeTransport *bleTransport = new FakeTransport();
    bleTransport->asynchronousOpen = true;
    TestMeshCoreProtocol protocol2(bleTransport);
    protocol2.setBackend("ble");
    if (protocol2.getBackend() != "ble") {
        std::cerr << "setBackend did not set backend correctly\n";
        return 1;
    }

    // Test BLE transport creation and behavior
    if (!protocol2.connectToDevice()) {
        std::cerr << "connectToDevice failed with ble backend\n";
        return 1;
    }
    if (protocol2.getProtocolState() != MeshCoreProtocol::Connecting ||
        !bleTransport->sentPayloads.isEmpty()) {
        std::cerr << "BLE sent APP_START before GATT connection completed\n";
        return 1;
    }
    bleTransport->completeConnection();
    if (protocol2.getProtocolState() != MeshCoreProtocol::Connected ||
        bleTransport->sentPayloads.size() != 1 ||
        bleTransport->sentPayloads.first() != QByteArray::fromHex("0100000000000000")) {
        std::cerr << "BLE connection did not start the Companion session exactly once\n";
        return 1;
    }

    protocol2.disconnect();
    if (!protocol2.connectToDevice()) {
        std::cerr << "BLE reconnect attempt failed\n";
        return 1;
    }
    bleTransport->completeConnection();
    if (protocol2.getProtocolState() != MeshCoreProtocol::Connected ||
        bleTransport->sentPayloads.size() != 2) {
        std::cerr << "BLE reconnect did not restart the Companion session\n";
        return 1;
    }

    // Verify that createTransport was called with "ble"
    if (protocol2.selectedBackend != QStringLiteral("ble")) {
        std::cerr << "createTransport was not called with 'ble'\n";
        return 1;
    }

    // Test 3: Invalid backend - should fail without calling createTransport
    FakeTransport invalidTransport;
    TestMeshCoreProtocol protocol3(&invalidTransport);
    protocol3.setBackend("invalid");
    bool result = protocol3.connectToDevice();

    // For invalid backends, connectToDevice should return false (no transport created)
    if (result) {
        std::cerr << "connectToDevice should fail with invalid backend\n";
        return 1;
    }

    // Check that createTransport was not called with invalid backend
    if (!protocol3.selectedBackend.isEmpty()) {
        std::cerr << "createTransport was unexpectedly called for invalid backend\n";
        return 1;
    }

    std::cout << "MeshCoreProtocol tests passed\n";
    return 0;
}
