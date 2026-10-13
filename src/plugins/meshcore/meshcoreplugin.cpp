#include "meshcoreplugin.h"
#include "meshcore.h"
#include "meshcorejoindialog.h"
#include <definitions/actiongroups.h>
#include <definitions/rosterindextyperole.h>
#include <interfaces/iaccountmanager.h>
#include <interfaces/imessagewidgets.h>
#include <interfaces/iprotocolaccount.h>
#include <interfaces/ipresence.h>
#include <utils/action.h>
#include <utils/menu.h>
#include <QTimer>

namespace
{
bool isHexIdentifier(const QString &value, int expectedLength)
{
    if (value.size() != expectedLength)
        return false;
    for (const QChar character : value) {
        const QChar lower = character.toLower();
        if (!(lower >= QLatin1Char('0') && lower <= QLatin1Char('9')) &&
            !(lower >= QLatin1Char('a') && lower <= QLatin1Char('f')))
            return false;
    }
    return true;
}

bool parseChannelConversationId(const QString &conversationId, QString &channelId)
{
    static const QString prefix = QStringLiteral("channel:");
    if (!conversationId.startsWith(prefix))
        return false;

    const QString candidate = conversationId.mid(prefix.size());
    bool ok = false;
    const uint index = candidate.toUInt(&ok);
    if (!ok || index > 7 || QString::number(index) != candidate)
        return false;

    channelId = candidate;
    return true;
}
}

MeshCorePlugin::MeshCorePlugin()
{
    FPluginManager = NULL;
    FOptionsManager = NULL;
    FRostersViewPlugin = NULL;
    FAccountManager = NULL;
    FMessageWidgets = NULL;
    FSelectedAccount = NULL;
    FConnectRequested = false;
    FPresenceShow = IPresence::Offline;
    FPresenceStatus.clear();
    FRequestedShow = IPresence::Online;
    FRequestedStatus.clear();
    FProtocol = NULL;
    FReconnectTimer = new QTimer(this);
    FReconnectTimer->setSingleShot(true);
    FReconnectDelayMs = 1000;
    connect(FReconnectTimer, &QTimer::timeout,
            this, &MeshCorePlugin::attemptReconnect);
}

MeshCorePlugin::MeshCorePlugin(MeshCoreProtocol *protocol)
    : MeshCorePlugin()
{
    FProtocol = protocol;
    if (FProtocol) {
        FProtocol->setParent(this);
        bindProtocolSignals();
    }
}

MeshCorePlugin::~MeshCorePlugin()
{
    if (FReconnectTimer)
        FReconnectTimer->stop();
    if (FProtocol) {
        FProtocol->disconnect();
        delete FProtocol;
        FProtocol = NULL;
    }
}

QObject *MeshCorePlugin::instance()
{
    return this;
}

int MeshCorePlugin::show() const
{
    return FPresenceShow;
}

QString MeshCorePlugin::status() const
{
    return FPresenceStatus;
}

bool MeshCorePlugin::setPresence(int AShow, const QString &AStatus)
{
    if (!FProtocol || (FSelectedAccount && !FSelectedAccount->isActive()))
        return false;

    if (AShow == IPresence::Offline) {
        FConnectRequested = false;
        if (FReconnectTimer)
            FReconnectTimer->stop();
        FReconnectDelayMs = 1000;
        FProtocol->disconnect();
        FPresenceShow = IPresence::Offline;
        FPresenceStatus.clear();
        emit protocolPresenceChanged(streamId(), FPresenceShow, FPresenceStatus);
        return true;
    }

    FConnectRequested = true;
    FRequestedShow = AShow;
    FRequestedStatus = AStatus;
    if (FProtocol->getProtocolState() == MeshCoreProtocol::Connected &&
        FProtocol->getDeviceState() == MeshCoreProtocol::DeviceReady) {
        FPresenceShow = FRequestedShow;
        FPresenceStatus = FRequestedStatus;
    } else {
        FPresenceShow = IPresence::Offline;
        FPresenceStatus.clear();
        if (FProtocol->getProtocolState() != MeshCoreProtocol::Connecting &&
            FProtocol->getProtocolState() != MeshCoreProtocol::Connected &&
            !FProtocol->connectToDevice())
            scheduleReconnect();
    }
    emit protocolPresenceChanged(streamId(), FPresenceShow, FPresenceStatus);
    return true;
}

QUuid MeshCorePlugin::pluginUuid() const
{
    // Placeholder UUID - will be replaced with real one later
    return QUuid("{1a1c4113-7105-42a8-809e-e6394f90c067}");
}

void MeshCorePlugin::pluginInfo(IPluginInfo *APluginInfo)
{
    APluginInfo->name = "MeshCore Protocol Plugin";
    APluginInfo->description = "Implements the MeshCore protocol for Vacuum Chat";
    APluginInfo->version = "0.1.0";
    APluginInfo->author = "Andreas Peters";
    APluginInfo->homePage = "https://github.com/andreaspeters/vacuum-chat";
}

bool MeshCorePlugin::initConnections(IPluginManager *APluginManager, int &AInitOrder)
{
    Q_UNUSED(AInitOrder);
    FPluginManager = APluginManager;

    IPlugin *plugin = FPluginManager
        ? FPluginManager->pluginInterface("IRostersViewPlugin").value(0, NULL) : NULL;
    if (plugin) {
        FRostersViewPlugin = qobject_cast<IRostersViewPlugin *>(plugin->instance());
    }

    plugin = FPluginManager
        ? FPluginManager->pluginInterface("IMessageWidgets").value(0, NULL) : NULL;
    FMessageWidgets = plugin
        ? qobject_cast<IMessageWidgets *>(plugin->instance()) : NULL;
    return true;
}

bool MeshCorePlugin::initObjects()
{
    // Perform initialization of objects
    return true;
}

bool MeshCorePlugin::initSettings()
{
    // Setup plugin settings
    return true;
}

bool MeshCorePlugin::startPlugin()
{
    IPlugin *accountManagerPlugin = FPluginManager
        ? FPluginManager->pluginInstance(ACCOUNTMANAGER_UUID) : NULL;
    FAccountManager = accountManagerPlugin
        ? qobject_cast<IAccountManager *>(accountManagerPlugin->instance()) : NULL;
    if (!FAccountManager)
        return true;

    QObject *accountManagerObject = FAccountManager->instance();
    if (!accountManagerObject)
        return true;

    connect(accountManagerObject, SIGNAL(appended(IAccount*)),
        this, SLOT(onAccountAppended(IAccount*)), Qt::UniqueConnection);
    connect(accountManagerObject, SIGNAL(shown(IAccount*)),
        this, SLOT(onAccountShown(IAccount*)), Qt::UniqueConnection);
    connect(accountManagerObject, SIGNAL(hidden(IAccount*)),
        this, SLOT(onAccountHidden(IAccount*)), Qt::UniqueConnection);

    foreach (IAccount *account, FAccountManager->accounts())
    {
        if (account && account->protocolKind() == IProtocolAccount::ProtocolMeshCore && account->isActive())
        {
            activateAccount(account);
            break;
        }
    }
    return true;
}

QString MeshCorePlugin::accountId() const
{
    if (FSelectedAccount)
        return FSelectedAccount->accountId().toString();
    return QString();
}

ProtocolAccountIdentifier MeshCorePlugin::accountIdentifier() const
{
    if (!FSelectedAccount || !FProtocol)
        return {};
    const QString publicKey = FProtocol->localPublicKeyHex();
    return publicKey.isEmpty()
        ? ProtocolAccountIdentifier()
        : ProtocolAccountIdentifier{tr("MeshCore public key"), publicKey};
}

QString MeshCorePlugin::protocol() const
{
    return QStringLiteral("meshcore");
}

QString MeshCorePlugin::streamId() const
{
    if (FSelectedAccount)
        return FSelectedAccount->accountId().toString();
    return QString();
}

QList<ProtocolRosterEntry> MeshCorePlugin::entries() const
{
    QList<ProtocolRosterEntry> result;
    if (!FProtocol)
        return result;

    for (const MeshCoreContact &contact : FProtocol->discoverContacts()) {
        ProtocolRosterEntry entry;
        entry.id = contact.id;
        entry.name = contact.name.isEmpty() ? contact.id.left(12) : contact.name;
        entry.isValid = !contact.id.isEmpty();
        if (entry.isValid)
            result.append(entry);
    }
    return result;
}

QList<ProtocolRoom> MeshCorePlugin::rooms() const
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

    if (FProtocol) {
        for (const MeshCoreChannel &channel : FProtocol->discoverChannels()) {
            ProtocolRoom room;
            room.id = QStringLiteral("channel:") + channel.id;
            room.name = channel.name;
            room.roomType = QStringLiteral("channel");
            room.isJoined = true;
            room.isAvailable = true;
            result.append(room);
        }
    }
    return result;
}

ProtocolRosterEntry MeshCorePlugin::entry(const QString &AId) const
{
    for (const ProtocolRosterEntry &contact : entries())
        if (contact.id == AId)
            return contact;
    return ProtocolRosterEntry();
}

ProtocolRoom MeshCorePlugin::room(const QString &AId) const
{
    for (const ProtocolRoom &conversation : rooms())
        if (conversation.id == AId)
            return conversation;
    return ProtocolRoom();
}

bool MeshCorePlugin::conversationIdForAddress(const Jid &address,
                                               ConversationId &conversationId) const
{
    conversationId.clear();
    if (!address.isValid() || address.domain() != QStringLiteral("meshcore.local") ||
        !address.resource().isEmpty())
        return false;

    const QString local = address.node();
    const QString prefixContact = QStringLiteral("contact-prefix-");
    if (local.startsWith(prefixContact)) {
        const QString keyPrefix = local.mid(prefixContact.size());
        if (!isHexIdentifier(keyPrefix, 12))
            return false;
        conversationId = QStringLiteral("contact-prefix:") + keyPrefix.toLower();
        return true;
    }

    const QString fullContact = QStringLiteral("contact-");
    if (local.startsWith(fullContact)) {
        const QString publicKey = local.mid(fullContact.size());
        if (!isHexIdentifier(publicKey, 64))
            return false;
        conversationId = publicKey.toLower();
        return true;
    }

    const QString channelPrefix = QStringLiteral("channel-");
    if (local.startsWith(channelPrefix)) {
        const QString channelId = local.mid(channelPrefix.size());
        bool ok = false;
        const uint index = channelId.toUInt(&ok);
        if (!ok || index > 7 || QString::number(index) != channelId)
            return false;
        conversationId = QStringLiteral("channel:") + channelId;
        return true;
    }
    return false;
}

Jid MeshCorePlugin::addressForConversation(const ConversationId &conversationId) const
{
    const QString prefixContact = QStringLiteral("contact-prefix:");
    if (conversationId.startsWith(prefixContact)) {
        const QString keyPrefix = conversationId.mid(prefixContact.size());
        if (isHexIdentifier(keyPrefix, 12))
            return Jid::fromUserInput(QStringLiteral("contact-prefix-") + keyPrefix.toLower() +
                                      QStringLiteral("@meshcore.local"));
        return Jid();
    }

    const QString fullContact = QStringLiteral("contact-");
    if (isHexIdentifier(conversationId, 64))
        return Jid::fromUserInput(fullContact + conversationId.toLower() +
                                  QStringLiteral("@meshcore.local"));

    QString channelId;
    if (parseChannelConversationId(conversationId, channelId))
        return Jid::fromUserInput(QStringLiteral("channel-") + channelId +
                                  QStringLiteral("@meshcore.local"));
    return Jid();
}

QString MeshCorePlugin::conversationDisplayName(const ConversationId &conversationId) const
{
    if (!FProtocol)
        return conversationId;

    QString channelId;
    if (parseChannelConversationId(conversationId, channelId)) {
        for (const MeshCoreChannel &channel : FProtocol->discoverChannels()) {
            if (channel.id == channelId)
                return channel.name.isEmpty() ? QStringLiteral("Channel %1").arg(channelId)
                                              : channel.name;
        }
        return conversationId;
    }

    QString contactKey = conversationId;
    const QString prefixContact = QStringLiteral("contact-prefix:");
    const bool isPrefix = conversationId.startsWith(prefixContact);
    if (isPrefix)
        contactKey = conversationId.mid(prefixContact.size());
    for (const MeshCoreContact &contact : FProtocol->discoverContacts()) {
        const bool matches = isPrefix
            ? contact.id.startsWith(contactKey, Qt::CaseInsensitive)
            : contact.id.compare(contactKey, Qt::CaseInsensitive) == 0;
        if (matches)
            return contact.name.isEmpty() ? contact.id.left(12) : contact.name;
    }
    return isPrefix ? contactKey : conversationId;
}

bool MeshCorePlugin::sendMessage(const BasicMessage &message)
{
    if (!FProtocol || message.conversationId().isEmpty() || message.body().isEmpty() ||
        (!message.protocol().isEmpty() && message.protocol() != protocol()))
        return false;

    QString channelId;
    if (parseChannelConversationId(message.conversationId(), channelId))
        return FProtocol->sendGroupMessage(message.conversationId(), message.body());
    return FProtocol->sendMessage(message.conversationId(), message.body());
}

QList<BasicMessage> MeshCorePlugin::conversationHistory(const ConversationId &conversationId) const
{
    return FProtocol ? FProtocol->conversationHistory(conversationId) : QList<BasicMessage>();
}

void MeshCorePlugin::activateAccount(IAccount *account)
{
    if (account == FSelectedAccount && FProtocol)
        return;

    const QString previousStreamId = streamId();

    if (FReconnectTimer)
        FReconnectTimer->stop();
    FConnectRequested = false;
    FPresenceShow = IPresence::Offline;
    FPresenceStatus.clear();
    FReconnectDelayMs = 1000;

    if (!previousStreamId.isEmpty())
        emit protocolPresenceClosed(previousStreamId);

    if (FProtocol) {
        FProtocol->disconnect();
        delete FProtocol;
        FProtocol = NULL;
    }
    FSelectedAccount = account;

    if (FSelectedAccount && FSelectedAccount->protocolKind() == IProtocolAccount::ProtocolMeshCore &&
        FSelectedAccount->isActive()) {
        const OptionsNode options = FSelectedAccount->optionsNode();
        const QString backend = options.value("meshcore.transport").toString().trimmed().toLower();
        const QString endpoint = backend == QStringLiteral("ble")
            ? options.value("meshcore.mac").toString().trimmed()
            : options.value("meshcore.port").toString().trimmed();

        FProtocol = new MeshCoreProtocol(this);
        bindProtocolSignals();
        FProtocol->initialize(endpoint);
        FProtocol->setBackend(backend);
        emit protocolPresenceChanged(streamId(), FPresenceShow, FPresenceStatus);
    }

    emit protocolRosterChanged();
}

void MeshCorePlugin::bindProtocolSignals()
{
    if (!FProtocol)
        return;

    connect(FProtocol, &MeshCoreProtocol::messageReceived,
            this, &MeshCorePlugin::onProtocolMessageReceived, Qt::UniqueConnection);
    connect(FProtocol, &MeshCoreProtocol::contactsChanged,
            this, &MeshCorePlugin::onContactsChanged, Qt::UniqueConnection);
    connect(FProtocol, &MeshCoreProtocol::channelsChanged,
            this, &MeshCorePlugin::onChannelsChanged, Qt::UniqueConnection);
    connect(FProtocol, &MeshCoreProtocol::deviceStateChanged, this,
            [this](MeshCoreProtocol::DeviceState state) {
        if (state == MeshCoreProtocol::DeviceReady) {
            FReconnectDelayMs = 1000;
            if (FReconnectTimer)
                FReconnectTimer->stop();
            if (FConnectRequested &&
                (FPresenceShow != FRequestedShow || FPresenceStatus != FRequestedStatus)) {
                FPresenceShow = FRequestedShow;
                FPresenceStatus = FRequestedStatus;
                emit protocolPresenceChanged(streamId(), FPresenceShow, FPresenceStatus);
            }
        } else if ((state == MeshCoreProtocol::DeviceOffline ||
                    state == MeshCoreProtocol::DeviceError) && FConnectRequested) {
            if (FPresenceShow != IPresence::Offline || !FPresenceStatus.isEmpty()) {
                FPresenceShow = IPresence::Offline;
                FPresenceStatus.clear();
                emit protocolPresenceChanged(streamId(), FPresenceShow, FPresenceStatus);
            }
            scheduleReconnect();
        }
    });
    connect(FProtocol, &MeshCoreProtocol::disconnected, this, [this]() {
        if (FConnectRequested) {
            if (FPresenceShow != IPresence::Offline || !FPresenceStatus.isEmpty()) {
                FPresenceShow = IPresence::Offline;
                FPresenceStatus.clear();
                emit protocolPresenceChanged(streamId(), FPresenceShow, FPresenceStatus);
            }
            scheduleReconnect();
        }
    });
}

void MeshCorePlugin::onContactsChanged()
{
    qWarning() << "[MC-SYNC] roster provider received contactsChanged; contacts"
               << (FProtocol ? FProtocol->discoverContacts().size() : 0);
    emit protocolRosterChanged();
}

void MeshCorePlugin::onChannelsChanged()
{
    qWarning() << "[MC-SYNC] roster provider received channelsChanged; rooms"
               << (FProtocol ? FProtocol->discoverChannels().size() : 0);
    emit protocolRosterChanged();
}

IProtocolCapabilities::Capabilities MeshCorePlugin::capabilitiesForAccount(
    const AccountId &accountId, const ConversationId &targetId) const
{
    if (accountId != this->accountId() || !FSelectedAccount ||
        !FSelectedAccount->isActive() || !FProtocol)
        return IProtocolCapabilities::Capabilities();

    const OptionsNode options = FSelectedAccount->optionsNode();
    const QString backend = FProtocol->getBackend();
    const bool isConfigured = backend == QStringLiteral("ble")
        ? !options.value(QStringLiteral("meshcore.mac")).toString().trimmed().isEmpty()
        : backend == QStringLiteral("usb") &&
            !options.value(QStringLiteral("meshcore.port")).toString().trimmed().isEmpty();
    if (!isConfigured)
        return IProtocolCapabilities::Capabilities();

    IProtocolCapabilities::Capabilities capabilities(IProtocolCapabilities::CapabilitySetPresence);
    if (FProtocol->getProtocolState() != MeshCoreProtocol::Connected)
        return capabilities;

    capabilities |= IProtocolCapabilities::CapabilityAddContact;
    // Advert support is a connected-session capability, not an indication that
    // the management channel is idle at this exact instant. The command path
    // rechecks readiness when the user invokes the action.
    capabilities |= IProtocolCapabilities::CapabilitySendZeroHopAdvert;
    capabilities |= IProtocolCapabilities::CapabilitySendFloodAdvert;
    if (!targetId.isEmpty())
        capabilities |= IProtocolCapabilities::CapabilityViewHistory;
    return capabilities;
}

bool MeshCorePlugin::showAddContactDialog(const AccountId &accountId)
{
    if (!hasCapabilities(accountId, IProtocolCapabilities::CapabilityAddContact))
        return false;
    showJoinChatDialog(accountId);
    return true;
}

bool MeshCorePlugin::sendSelfAdvert(const AccountId &accountId, AdvertType type)
{
    IProtocolCapabilities::Capability capability;
    switch (type) {
    case AdvertType::ZeroHop:
        capability = IProtocolCapabilities::CapabilitySendZeroHopAdvert;
        break;
    case AdvertType::Flood:
        capability = IProtocolCapabilities::CapabilitySendFloodAdvert;
        break;
    default:
        return false;
    }
    return FProtocol && hasCapabilities(accountId, capability) &&
        FProtocol->sendSelfAdvert(type == AdvertType::Flood);
}

void MeshCorePlugin::showJoinChatDialog(const QString &boundAccountId)
{
    if (!FProtocol || !FSelectedAccount || !FSelectedAccount->isActive() ||
        FProtocol->getProtocolState() != MeshCoreProtocol::Connected ||
        FSelectedAccount->accountId().toString() != boundAccountId)
        return;

    QWidget *parent = FRostersViewPlugin && FRostersViewPlugin->rostersView()
        ? FRostersViewPlugin->rostersView()->instance() : nullptr;
    MeshCoreJoinDialog dialog(parent);
    auto updateLists = [this, &dialog]() {
        if (!FProtocol)
            return;
        QList<QPair<QString, QString>> channels;
        for (const MeshCoreChannel &channel : FProtocol->discoverChannels())
            channels.append(qMakePair(channel.id, channel.name));
        dialog.setChannels(channels);
        dialog.setAvailableChannelSlots(FProtocol->availableChannelSlots());

        QList<QPair<QString, QString>> contacts;
        for (const MeshCoreContact &contact : FProtocol->discoverContacts())
            contacts.append(qMakePair(contact.id, contact.name));
        dialog.setContacts(contacts);
    };
    updateLists();

    connect(FProtocol, &MeshCoreProtocol::channelsChanged, &dialog, updateLists);
    connect(FProtocol, &MeshCoreProtocol::contactsChanged, &dialog, updateLists);
    connect(FProtocol, &MeshCoreProtocol::channelConfigurationFinished,
            &dialog, &MeshCoreJoinDialog::setChannelOperationResult);
    connect(FProtocol, &MeshCoreProtocol::contactAdditionFinished,
            &dialog, &MeshCoreJoinDialog::setContactOperationResult);
    connect(FProtocol, &QObject::destroyed, &dialog, &QDialog::reject);

    connect(&dialog, &MeshCoreJoinDialog::setChannelRequested, this,
            [this, &dialog, boundAccountId](int channelIndex, const QString &name,
                                           const QByteArray &secret) {
        if (!FProtocol || !FSelectedAccount ||
            FSelectedAccount->accountId().toString() != boundAccountId ||
            !FProtocol->setChannel(channelIndex, name, secret))
            dialog.setChannelOperationResult(channelIndex, false,
                tr("The radio is offline, syncing, or rejected the channel data."));
    });
    connect(&dialog, &MeshCoreJoinDialog::addContactRequested, this,
            [this, &dialog, boundAccountId](const QString &publicKeyHex,
                                           const QString &name) {
        if (!FProtocol || !FSelectedAccount ||
            FSelectedAccount->accountId().toString() != boundAccountId ||
            !FProtocol->addContact(publicKeyHex, name))
            dialog.setContactOperationResult(publicKeyHex, false,
                tr("The radio is offline, syncing, or rejected the contact data."));
    });
    connect(&dialog, &MeshCoreJoinDialog::conversationReady, this,
            [this, boundAccountId](const QString &conversationId) {
        if (!FSelectedAccount || !FMessageWidgets ||
            FSelectedAccount->accountId().toString() != boundAccountId)
            return;
        IChatWindow *window = FMessageWidgets->getConversationWindow(
            boundAccountId, conversationId);
        if (window)
            window->showTabPage();
    });
    dialog.exec();
}

void MeshCorePlugin::onProtocolMessageReceived(const BasicMessage &message)
{
    emit protocolMessageReceived(message);
}

void MeshCorePlugin::scheduleReconnect()
{
    if (!FConnectRequested || !FReconnectTimer || !FProtocol ||
        (FSelectedAccount && !FSelectedAccount->isActive()) ||
        FProtocol->getProtocolState() == MeshCoreProtocol::Connected ||
        FProtocol->getProtocolState() == MeshCoreProtocol::Connecting ||
        FReconnectTimer->isActive())
        return;

    FReconnectTimer->start(FReconnectDelayMs);
    FReconnectDelayMs = qMin(FReconnectDelayMs * 2, 30000);
}

void MeshCorePlugin::attemptReconnect()
{
    if (!FConnectRequested || !FProtocol ||
        (FSelectedAccount && !FSelectedAccount->isActive()))
        return;
    if (FProtocol->getProtocolState() == MeshCoreProtocol::Connected ||
        FProtocol->getProtocolState() == MeshCoreProtocol::Connecting)
        return;

    if (!FProtocol->connectToDevice())
        scheduleReconnect();
}

void MeshCorePlugin::onAccountAppended(IAccount *AAccount)
{
    if (AAccount && AAccount->protocolKind() == IProtocolAccount::ProtocolMeshCore && AAccount->isActive())
        activateAccount(AAccount);
}

void MeshCorePlugin::onAccountShown(IAccount *AAccount)
{
    if (AAccount && AAccount->protocolKind() == IProtocolAccount::ProtocolMeshCore && AAccount->isActive())
        activateAccount(AAccount);
}

void MeshCorePlugin::onAccountHidden(IAccount *AAccount)
{
    if (!AAccount || AAccount != FSelectedAccount)
        return;

    IAccount *nextAccount = NULL;
    if (FAccountManager) {
        for (IAccount *account : FAccountManager->accounts()) {
            if (account != AAccount && account->protocolKind() == IProtocolAccount::ProtocolMeshCore &&
                account->isActive()) {
                nextAccount = account;
                break;
            }
        }
    }
    activateAccount(nextAccount);
}
