#include "ax25chatplugin.h"

#include "ax25kisscodec.h"
#include "ax25kissserialtransport.h"
#include "ax25flexpacketpipetransport.h"
#include "ax25chatmessagerouting.h"
#include "aprsmessageengine.h"
#include "aprsroomcodec.h"
#include "aprsroomstore.h"

#include <interfaces/iaccountmanager.h>
#include <interfaces/iprotocolaccount.h>
#include <interfaces/ipresence.h>

#include <QDir>
#include <QDebug>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QSettings>
#include <QTimer>

#include <algorithm>

namespace
{
const QString kAx25Protocol = QStringLiteral("ax25");
const QString kAx25AddressDomain = QStringLiteral("ax25.invalid");
const QString kAx25GroupAddressDomain = QStringLiteral("group.ax25.invalid");
const QString kGroupConversationPrefix = QStringLiteral("aprs-group:");
const QString kAccountTransportKey = QStringLiteral("ax25.transport");
const QString kAccountCallsignKey = QStringLiteral("ax25.callsign");
const QString kAccountPortKey = QStringLiteral("ax25.port");
const QString kAccountBaudRateKey = QStringLiteral("ax25.baud-rate");

QString accountGroup(const QString &accountId)
{
    return QStringLiteral("accounts/%1").arg(accountId);
}

QString groupNameForConversationId(const QString &conversationId)
{
    if (!conversationId.startsWith(kGroupConversationPrefix, Qt::CaseInsensitive))
        return QString();
    const QString groupName = conversationId.mid(kGroupConversationPrefix.size()).trimmed().toUpper();
    return AprsRoom::Codec::validateRoomName(groupName) ? groupName : QString();
}

QString groupConversationId(const QString &groupName)
{
    const QString canonicalName = groupName.trimmed().toUpper();
    return AprsRoom::Codec::validateRoomName(canonicalName)
        ? kGroupConversationPrefix + canonicalName : QString();
}
}

Ax25ChatPlugin::Ax25ChatPlugin(QObject *parent)
    : QObject(parent),
      m_serialTransport(new Ax25KissSerialTransport(this)),
      m_pipeTransport(new Ax25FlexPacketPipeTransport(this)),
      m_transport(m_serialTransport),
      m_show(IPresence::Offline)
{
    connect(m_serialTransport, &Ax25KissSerialTransport::disconnected,
            this, &Ax25ChatPlugin::onTransportDisconnected);
    connect(m_serialTransport, &Ax25KissSerialTransport::transportError,
            this, &Ax25ChatPlugin::onTransportError);
    connect(m_pipeTransport, &Ax25FlexPacketPipeTransport::disconnected,
            this, &Ax25ChatPlugin::onTransportDisconnected);
    connect(m_pipeTransport, &Ax25FlexPacketPipeTransport::transportError,
            this, &Ax25ChatPlugin::onTransportError);
}

Ax25ChatPlugin::~Ax25ChatPlugin()
{
    if (m_retryTimer)
        m_retryTimer->stop();
    if (m_serialTransport)
        m_serialTransport->close();
    if (m_pipeTransport)
        m_pipeTransport->close();
    delete m_engine;
    m_engine = nullptr;
    delete m_serialTransport;
    delete m_pipeTransport;
    m_serialTransport = nullptr;
    m_pipeTransport = nullptr;
    m_transport = nullptr;
}

QObject *Ax25ChatPlugin::instance()
{
    return this;
}

QUuid Ax25ChatPlugin::pluginUuid() const
{
    return QUuid(QStringLiteral("{4a1aabd7-738e-43c1-a89a-5e5b6cd8af54}"));
}

void Ax25ChatPlugin::pluginInfo(IPluginInfo *pluginInfo)
{
    if (!pluginInfo)
        return;
    pluginInfo->name = tr("APRS Chat");
    pluginInfo->description = tr("APRS messaging over AX.25/KISS and FlexPacket");
    pluginInfo->version = QStringLiteral("0.1.0");
    pluginInfo->author = tr("Vacuum Chat contributors");
}

bool Ax25ChatPlugin::initConnections(IPluginManager *pluginManager, int &initOrder)
{
    Q_UNUSED(initOrder);
    m_pluginManager = pluginManager;
    return true;
}

bool Ax25ChatPlugin::initObjects()
{
    resetAprsEngine();
    if (!m_retryTimer) {
        m_retryTimer = new QTimer(this);
        m_retryTimer->setInterval(100);
        connect(m_retryTimer, &QTimer::timeout, this, &Ax25ChatPlugin::onRetryTimer);
    }
    m_clock.start();
    m_retryTimer->start();
    return true;
}

bool Ax25ChatPlugin::initSettings()
{
    return true;
}

bool Ax25ChatPlugin::startPlugin()
{
    IPlugin *accountManagerPlugin = m_pluginManager
        ? m_pluginManager->pluginInstance(ACCOUNTMANAGER_UUID) : nullptr;
    m_accountManager = accountManagerPlugin
        ? qobject_cast<IAccountManager *>(accountManagerPlugin->instance()) : nullptr;
    if (!m_accountManager || !m_accountManager->instance())
        return true;

    QObject *accountManagerObject = m_accountManager->instance();
    connect(accountManagerObject, SIGNAL(appended(IAccount*)),
            this, SLOT(onAccountAppended(IAccount*)), Qt::UniqueConnection);
    connect(accountManagerObject, SIGNAL(shown(IAccount*)),
            this, SLOT(onAccountShown(IAccount*)), Qt::UniqueConnection);
    connect(accountManagerObject, SIGNAL(hidden(IAccount*)),
            this, SLOT(onAccountHidden(IAccount*)), Qt::UniqueConnection);

    for (IAccount *account : m_accountManager->accounts()) {
        if (account && account->protocolKind() == IProtocolAccount::ProtocolAx25 &&
            account->isActive()) {
            activateAccount(account);
            break;
        }
    }
    return true;
}

QString Ax25ChatPlugin::accountId() const
{
    return m_selectedAccount ? m_selectedAccount->accountId().toString() : QString();
}

ProtocolAccountIdentifier Ax25ChatPlugin::accountIdentifier() const
{
    if (!m_selectedAccount)
        return {};
    const QString callsign = m_selectedAccount->optionsNode()
        .value(kAccountCallsignKey).toString().trimmed().toUpper();
    return callsign.isEmpty()
        ? ProtocolAccountIdentifier()
        : ProtocolAccountIdentifier{tr("AX.25 callsign"), callsign};
}

QString Ax25ChatPlugin::protocol() const
{
    return kAx25Protocol;
}

QString Ax25ChatPlugin::streamId() const
{
    return accountId();
}

QList<ProtocolRosterEntry> Ax25ChatPlugin::entries() const
{
    QList<ProtocolRosterEntry> result;
    if (!m_selectedAccount)
        return result;

    for (const QString &callsign : contactCallsigns()) {
        ProtocolRosterEntry contact;
        contact.id = callsign;
        contact.name = callsign;
        contact.isValid = true;
        result.append(contact);
    }
    return result;
}

QList<ProtocolRoom> Ax25ChatPlugin::rooms() const
{
    QList<ProtocolRoom> result;
    for (const ProtocolRosterEntry &contact : entries()) {
        ProtocolRoom conversation;
        conversation.id = contact.id;
        conversation.name = contact.name;
        conversation.isDirect = true;
        conversation.isJoined = true;
        conversation.isAvailable = true;
        conversation.members.append(contact);
        result.append(conversation);
    }
    if (!m_selectedAccount)
        return result;
    for (const QString &groupName : AprsRoom::GroupStore::groupNames(
             settingsFilePath(), accountId())) {
        ProtocolRoom conversation;
        conversation.id = groupConversationId(groupName);
        conversation.name = groupName;
        conversation.membership = QStringLiteral("desired");
        conversation.isJoined = true;
        conversation.isDirect = false;
        conversation.isAvailable = transportIsConnected();
        result.append(conversation);
    }
    return result;
}

ProtocolRosterEntry Ax25ChatPlugin::entry(const QString &id) const
{
    const QString callsign = canonicalCallsign(id);
    for (const ProtocolRosterEntry &contact : entries()) {
        if (contact.id == callsign)
            return contact;
    }
    return ProtocolRosterEntry();
}

ProtocolRoom Ax25ChatPlugin::room(const QString &id) const
{
    const QString groupName = groupNameForConversationId(id);
    if (!groupName.isEmpty()) {
        const QString groupId = groupConversationId(groupName);
        for (const ProtocolRoom &conversation : rooms())
            if (conversation.id == groupId)
                return conversation;
        return ProtocolRoom();
    }
    const QString callsign = canonicalCallsign(id);
    for (const ProtocolRoom &conversation : rooms()) {
        if (conversation.id == callsign)
            return conversation;
    }
    return ProtocolRoom();
}

bool Ax25ChatPlugin::conversationIdForAddress(const Jid &address,
                                             ConversationId &conversationId) const
{
    conversationId.clear();
    if (!address.isValid() || !address.resource().isEmpty())
        return false;

    if (address.domain().compare(kAx25GroupAddressDomain, Qt::CaseInsensitive) == 0) {
        conversationId = groupConversationId(address.node());
        return !conversationId.isEmpty();
    }
    if (address.domain().compare(kAx25AddressDomain, Qt::CaseInsensitive) != 0)
        return false;

    conversationId = canonicalCallsign(address.node());
    return !conversationId.isEmpty();
}

Jid Ax25ChatPlugin::addressForConversation(const ConversationId &conversationId) const
{
    const QString groupName = groupNameForConversationId(conversationId);
    if (!groupName.isEmpty())
        return Jid::fromUserInput(QStringLiteral("%1@%2").arg(groupName, kAx25GroupAddressDomain));
    const QString callsign = canonicalCallsign(conversationId);
    if (callsign.isEmpty())
        return Jid();
    return Jid::fromUserInput(QStringLiteral("%1@%2").arg(callsign, kAx25AddressDomain));
}

QString Ax25ChatPlugin::conversationDisplayName(const ConversationId &conversationId) const
{
    const QString groupName = groupNameForConversationId(conversationId);
    if (!groupName.isEmpty())
        return groupName;
    const QString callsign = canonicalCallsign(conversationId);
    return callsign.isEmpty() ? conversationId : callsign;
}

bool Ax25ChatPlugin::sendMessage(const BasicMessage &message)
{
    if (!m_selectedAccount || !m_selectedAccount->isActive() || !m_engine ||
        !m_transport || !transportIsConnected() || message.body().isEmpty() ||
        (!message.protocol().isEmpty() && message.protocol() != protocol()))
        return false;

    const QString groupName = groupNameForConversationId(message.conversationId());
    const bool isGroupMessage = !groupName.isEmpty();
    const QString destination = isGroupMessage ? groupName
        : canonicalCallsign(message.conversationId());
    const QString localCallsign = canonicalCallsign(m_selectedAccount->optionsNode()
        .value(kAccountCallsignKey).toString());
    if (destination.isEmpty() || localCallsign.isEmpty())
        return false;
    if (isGroupMessage) {
        if (!AprsRoom::GroupStore::groupNames(settingsFilePath(), accountId()).contains(groupName))
            return false;
    } else if (destination == localCallsign || !addContactCallsign(destination)) {
        return false;
    }

    QString messageId;
    QString sendError;
    const bool sent = isGroupMessage
        ? m_engine->sendGroupMessage(groupName, message.body(), m_clock.elapsed(),
                                     &messageId, &sendError)
        : m_engine->sendMessage(destination, message.body(), m_clock.elapsed(),
                                &messageId, &sendError);
    if (!sent) {
        if (!sendError.isEmpty())
            qWarning().noquote() << "[aprs-chat] message send failed:" << sendError;
        return false;
    }

    const QString conversationId = isGroupMessage ? groupConversationId(groupName) : destination;
    emit protocolMessageReceived(Ax25ChatMessageRouting::createOutgoingMessage(
        conversationId, localCallsign, message.body(), messageId, QDateTime::currentDateTimeUtc()));
    return true;
}

int Ax25ChatPlugin::show() const
{
    return m_show;
}

QString Ax25ChatPlugin::status() const
{
    return m_status;
}

bool Ax25ChatPlugin::setPresence(int requestedShow, const QString &requestedStatus)
{
    if (!m_selectedAccount || !m_selectedAccount->isActive() ||
        m_selectedAccount->protocolKind() != IProtocolAccount::ProtocolAx25)
        return false;

    if (requestedShow == IPresence::Offline) {
        updateOfflinePresence();
        return true;
    }

    const OptionsNode options = m_selectedAccount->optionsNode();
    const QString transport = options.value(kAccountTransportKey).toString().trimmed();
    const QString callsign = canonicalCallsign(options.value(kAccountCallsignKey).toString());
    const QString port = options.value(kAccountPortKey).toString().trimmed();
    const int baudRate = options.value(kAccountBaudRateKey).toInt();
    if (callsign.isEmpty())
        return false;

    if (transport == QStringLiteral("kiss-serial")) {
        if (port.isEmpty())
            return false;
        if (m_transport != m_serialTransport) {
            m_transport = m_serialTransport;
            resetAprsEngine();
        }
        if (!m_serialTransport->isConnected() &&
            !m_serialTransport->open(port, callsign, baudRate)) {
            updateOfflinePresence();
            return false;
        }
    } else if (transport == QStringLiteral("flexpacket-pipe")) {
        if (m_transport != m_pipeTransport) {
            m_transport = m_pipeTransport;
            resetAprsEngine();
        }
        if (!m_pipeTransport->isConnected() && !m_pipeTransport->open(callsign, 0)) {
            updateOfflinePresence();
            return false;
        }
    } else {
        return false;
    }

    m_show = requestedShow;
    m_status = requestedStatus;
    sendDesiredGroupJoins();
    emit protocolPresenceChanged(streamId(), m_show, m_status);
    return true;
}

IProtocolCapabilities::Capabilities Ax25ChatPlugin::capabilitiesForAccount(
    const AccountId &requestedAccountId, const ConversationId &targetId) const
{
    if (!isSelectedAccount(requestedAccountId) || !m_selectedAccount->isValid())
        return IProtocolCapabilities::Capabilities();

    IProtocolCapabilities::Capabilities result(
        IProtocolCapabilities::CapabilitySetPresence);
    if (!settingsFilePath().isEmpty()) {
        result |= IProtocolCapabilities::CapabilityAddContact |
            IProtocolCapabilities::CapabilityJoinRoom;
        const QString groupName = groupNameForConversationId(targetId);
        if (!groupName.isEmpty() && AprsRoom::GroupStore::groupNames(
                settingsFilePath(), accountId()).contains(groupName))
            result |= IProtocolCapabilities::CapabilityLeaveRoom;
    }
    return result;
}

bool Ax25ChatPlugin::showAddContactDialog(const AccountId &requestedAccountId)
{
    if (!hasCapabilities(requestedAccountId, IProtocolCapabilities::CapabilityAddContact))
        return false;

    bool accepted = false;
    const QString input = QInputDialog::getText(nullptr, tr("Add AX.25 contact"),
        tr("Remote callsign:"), QLineEdit::Normal, QString(), &accepted);
    if (!accepted)
        return true;

    const QString callsign = canonicalCallsign(input.trimmed().toUpper());
    if (callsign.isEmpty()) {
        QMessageBox::warning(nullptr, tr("Invalid callsign"),
            tr("Enter a valid AX.25 callsign with an SSID from 0 to 15."));
        return true;
    }
    return addContactCallsign(callsign);
}

bool Ax25ChatPlugin::showJoinRoomDialog(const AccountId &requestedAccountId)
{
    if (!hasCapabilities(requestedAccountId, IProtocolCapabilities::CapabilityJoinRoom))
        return false;

    bool accepted = false;
    const QString input = QInputDialog::getText(nullptr, tr("Join APRS group"),
        tr("ANSRVR group name:"), QLineEdit::Normal, QString(), &accepted);
    if (!accepted)
        return true;

    QString error;
    if (!addGroupSubscription(input, &error))
        QMessageBox::warning(nullptr, tr("Could not join APRS group"),
            error.isEmpty() ? tr("The group subscription could not be saved.") : error);
    return true;
}

bool Ax25ChatPlugin::leaveRoom(const AccountId &requestedAccountId,
                               const ConversationId &roomId)
{
    if (!hasCapabilities(requestedAccountId, IProtocolCapabilities::CapabilityLeaveRoom, roomId))
        return false;
    const QString groupName = groupNameForConversationId(roomId);
    if (groupName.isEmpty())
        return false;

    QString error;
    if (!AprsRoom::GroupStore::removeGroup(settingsFilePath(), accountId(), groupName, &error)) {
        qWarning().noquote() << "[aprs-chat] could not remove ANSRVR group subscription:" << error;
        return false;
    }
    m_joinCommandsSent.remove(groupName);
    if (m_engine && transportIsConnected()) {
        QString messageId;
        if (!m_engine->sendGroupLeaveCommand(groupName, m_clock.elapsed(), &messageId, &error))
            qWarning().noquote() << "[aprs-chat] could not queue ANSRVR leave command:" << error;
    }
    emit protocolRosterChanged();
    return true;
}

void Ax25ChatPlugin::onAccountAppended(IAccount *account)
{
    if (account && account->protocolKind() == IProtocolAccount::ProtocolAx25 &&
        account->isActive() && !m_selectedAccount)
        activateAccount(account);
}

void Ax25ChatPlugin::onAccountShown(IAccount *account)
{
    if (account && account->protocolKind() == IProtocolAccount::ProtocolAx25 &&
        account->isActive())
        activateAccount(account);
}

void Ax25ChatPlugin::onAccountHidden(IAccount *account)
{
    if (!account || account != m_selectedAccount)
        return;

    IAccount *nextAccount = nullptr;
    if (m_accountManager) {
        for (IAccount *candidate : m_accountManager->accounts()) {
            if (candidate && candidate != account && candidate->isActive() &&
                candidate->protocolKind() == IProtocolAccount::ProtocolAx25) {
                nextAccount = candidate;
                break;
            }
        }
    }
    activateAccount(nextAccount);
}

void Ax25ChatPlugin::onIncomingMessage(const QString &source, const QString &messageId,
                                       const QString &text)
{
    const QString sender = canonicalCallsign(source);
    if (!m_selectedAccount)
        return;

    const QString localCallsign = canonicalCallsign(m_selectedAccount->optionsNode()
        .value(kAccountCallsignKey).toString());
    if (!Ax25ChatMessageRouting::isRemoteCallsign(sender, localCallsign))
        return;

    AprsRoom::RoomMessage roomMessage;
    if (sender != QStringLiteral("ANSRVR") &&
        AprsRoom::Codec::decode(text.toUtf8(), &roomMessage)) {
        const QStringList subscribedGroups =
            AprsRoom::GroupStore::groupNames(settingsFilePath(), accountId());
        if (!subscribedGroups.contains(roomMessage.roomName))
            return;

        const QString roomId = groupConversationId(roomMessage.roomName);
        const QString uniqueId = messageId.isEmpty()
            ? QStringLiteral("aprs:group:%1:%2:no-id:%3")
                  .arg(roomMessage.roomName, sender).arg(QDateTime::currentMSecsSinceEpoch())
            : QStringLiteral("aprs:group:%1:%2:%3")
                  .arg(roomMessage.roomName, sender, messageId);
        const BasicMessage message(uniqueId, roomId, sender, localCallsign, roomMessage.body,
            QDateTime::currentDateTimeUtc(), protocol(), BasicMessage::Incoming);
        emit protocolMessageReceived(message);
        return;
    }

    // ANSRVR confirmations remain service messages, not group-member posts.
    addContactCallsign(sender);
    const QString uniqueId = messageId.isEmpty()
        ? QStringLiteral("aprs:%1:no-id:%2").arg(sender).arg(QDateTime::currentMSecsSinceEpoch())
        : QStringLiteral("aprs:%1:%2").arg(sender, messageId);
    const BasicMessage message(uniqueId, sender, sender, localCallsign, text,
        QDateTime::currentDateTimeUtc(), protocol(), BasicMessage::Incoming);
    emit protocolMessageReceived(message);
}

void Ax25ChatPlugin::onTransportDisconnected()
{
    updateOfflinePresence();
}

void Ax25ChatPlugin::onTransportError(const QString &error)
{
    qWarning().noquote() << "[ax25chat] transport error:" << error;
    updateOfflinePresence();
}

void Ax25ChatPlugin::onRetryTimer()
{
    if (m_engine)
        m_engine->tick(m_clock.elapsed());
}

void Ax25ChatPlugin::activateAccount(IAccount *account)
{
    if (account && (account->protocolKind() != IProtocolAccount::ProtocolAx25 ||
                    !account->isActive()))
        account = nullptr;
    if (account == m_selectedAccount && m_engine)
        return;

    const QString previousStreamId = streamId();
    m_show = IPresence::Offline;
    m_status.clear();
    if (!previousStreamId.isEmpty())
        emit protocolPresenceClosed(previousStreamId);
    if (m_serialTransport)
        m_serialTransport->close();
    if (m_pipeTransport)
        m_pipeTransport->close();

    m_selectedAccount = account;
    const QString configuredTransport = account
        ? account->optionsNode().value(kAccountTransportKey).toString().trimmed()
        : QString();
    m_transport = configuredTransport == QStringLiteral("flexpacket-pipe")
        ? static_cast<Axcp::Transport *>(m_pipeTransport)
        : static_cast<Axcp::Transport *>(m_serialTransport);
    resetAprsEngine();
    emit protocolRosterChanged();
    if (!streamId().isEmpty())
        emit protocolPresenceChanged(streamId(), m_show, m_status);
}

void Ax25ChatPlugin::resetAprsEngine()
{
    delete m_engine;
    m_engine = nullptr;
    m_joinCommandsSent.clear();
    if (!m_transport)
        return;

    AprsChat::AprsMessageEngine::Config config;
    if (m_selectedAccount) {
        config.localCallsign = canonicalCallsign(m_selectedAccount->optionsNode()
            .value(kAccountCallsignKey).toString());
    }
    m_engine = new AprsChat::AprsMessageEngine(m_transport, config,
        AprsChat::AprsMessageEngine::MessageIdGenerator(), this);
    connect(m_engine, &AprsChat::AprsMessageEngine::incomingMessage,
            this, &Ax25ChatPlugin::onIncomingMessage);
}

void Ax25ChatPlugin::updateOfflinePresence()
{
    const QString currentStreamId = streamId();
    const bool changed = m_show != IPresence::Offline || !m_status.isEmpty();
    m_show = IPresence::Offline;
    m_status.clear();
    m_joinCommandsSent.clear();
    if (transportIsConnected()) {
        if (m_transport == m_pipeTransport)
            m_pipeTransport->close();
        else if (m_serialTransport)
            m_serialTransport->close();
    }
    if (changed && !currentStreamId.isEmpty())
        emit protocolPresenceChanged(currentStreamId, m_show, m_status);
}

bool Ax25ChatPlugin::transportIsConnected() const
{
    if (m_transport == m_pipeTransport)
        return m_pipeTransport && m_pipeTransport->isConnected();
    return m_serialTransport && m_serialTransport->isConnected();
}

bool Ax25ChatPlugin::isSelectedAccount(const AccountId &requestedAccountId) const
{
    return m_selectedAccount && m_selectedAccount->isActive() &&
        !requestedAccountId.isEmpty() && requestedAccountId == accountId();
}

QString Ax25ChatPlugin::settingsFilePath() const
{
    if (!m_pluginManager || m_pluginManager->homePath().trimmed().isEmpty())
        return QString();
    return QDir(m_pluginManager->homePath()).filePath(QStringLiteral("ax25chat.ini"));
}

QStringList Ax25ChatPlugin::contactCallsigns() const
{
    QStringList result;
    const QString path = settingsFilePath();
    if (path.isEmpty() || !m_selectedAccount)
        return result;

    QSettings settings(path, QSettings::IniFormat);
    settings.beginGroup(accountGroup(accountId()));
    const QStringList stored = settings.value(QStringLiteral("contacts")).toStringList();
    settings.endGroup();
    const QString localCallsign = canonicalCallsign(m_selectedAccount->optionsNode()
        .value(kAccountCallsignKey).toString());

    for (const QString &value : stored) {
        const QString callsign = canonicalCallsign(value);
        if (Ax25ChatMessageRouting::isRemoteCallsign(callsign, localCallsign) &&
            !result.contains(callsign))
            result.append(callsign);
    }
    std::sort(result.begin(), result.end());
    return result;
}

bool Ax25ChatPlugin::addContactCallsign(const QString &value)
{
    if (!m_selectedAccount || !m_selectedAccount->isActive())
        return false;
    const QString callsign = canonicalCallsign(value);
    const QString localCallsign = m_selectedAccount
        ? canonicalCallsign(m_selectedAccount->optionsNode().value(kAccountCallsignKey).toString())
        : QString();
    const QString path = settingsFilePath();
    if (!Ax25ChatMessageRouting::isRemoteCallsign(callsign, localCallsign) || path.isEmpty())
        return false;

    QStringList contacts = contactCallsigns();
    if (contacts.contains(callsign))
        return true;
    contacts.append(callsign);
    std::sort(contacts.begin(), contacts.end());

    QSettings settings(path, QSettings::IniFormat);
    settings.beginGroup(accountGroup(accountId()));
    settings.setValue(QStringLiteral("contacts"), contacts);
    settings.endGroup();
    settings.sync();
    if (settings.status() != QSettings::NoError)
        return false;

    emit protocolRosterChanged();
    return true;
}

bool Ax25ChatPlugin::addGroupSubscription(const QString &value, QString *error)
{
    if (error)
        error->clear();
    if (!m_selectedAccount || !m_selectedAccount->isActive() || settingsFilePath().isEmpty()) {
        if (error)
            *error = tr("An active AX.25 account and writable settings are required.");
        return false;
    }
    const QString groupName = value.trimmed().toUpper();
    if (!AprsRoom::Codec::validateRoomName(groupName, error))
        return false;

    const QStringList before = AprsRoom::GroupStore::groupNames(settingsFilePath(), accountId());
    if (!AprsRoom::GroupStore::addGroup(settingsFilePath(), accountId(), groupName, error))
        return false;
    if (transportIsConnected() && m_engine && !m_joinCommandsSent.contains(groupName)) {
        QString messageId;
        QString sendError;
        if (m_engine->sendGroupJoinCommand(groupName, m_clock.elapsed(), &messageId, &sendError))
            m_joinCommandsSent.insert(groupName);
        else if (error)
            *error = sendError;
    }
    if (!before.contains(groupName))
        emit protocolRosterChanged();
    return true;
}

void Ax25ChatPlugin::sendDesiredGroupJoins()
{
    if (!m_selectedAccount || !m_engine || !transportIsConnected())
        return;
    for (const QString &groupName : AprsRoom::GroupStore::groupNames(
             settingsFilePath(), accountId())) {
        if (m_joinCommandsSent.contains(groupName))
            continue;
        QString messageId;
        QString error;
        if (m_engine->sendGroupJoinCommand(groupName, m_clock.elapsed(), &messageId, &error))
            m_joinCommandsSent.insert(groupName);
        else if (!error.isEmpty())
            qWarning().noquote() << "[aprs-chat] could not queue ANSRVR rejoin command:" << error;
    }
}

QString Ax25ChatPlugin::canonicalCallsign(const QString &value)
{
    QByteArray frame;
    if (!Ax25Kiss::Ax25UiFrameCodec::encodeUiFrame(
            QStringLiteral("N0CALL"), value, QByteArray(), &frame))
        return QString();

    Ax25Kiss::Ax25UiFrame decoded;
    if (!Ax25Kiss::Ax25UiFrameCodec::decodeUiFrame(frame, &decoded))
        return QString();
    return decoded.destination;
}
