#ifndef MESHCOREPLUGIN_H
#define MESHCOREPLUGIN_H

#include <QObject>
#include <interfaces/ipluginmanager.h>
#include <interfaces/ioptionsmanager.h>
#include <interfaces/irostersview.h>
#include <interfaces/iprotocolroster.h>
#include <interfaces/iprotocolmessaging.h>
#include <interfaces/iprotocolaccount.h>
#include <interfaces/iprotocolcapabilities.h>
#include <interfaces/iprotocolcontactactions.h>
#include <interfaces/iprotocolpresence.h>
#include <interfaces/iaccountmanager.h>

class IAccount;
class IProtocolRoster;
class IMessageWidgets;
class MeshCoreProtocol;
class QTimer;

class MeshCorePlugin : public QObject, public IPlugin, public IProtocolRoster, public IProtocolMessaging, public IProtocolCapabilities, public IProtocolContactActions, public IProtocolPresence
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "Vacuum.Core.IPlugin/1.0" FILE "meshcore.json")
    Q_INTERFACES(IPlugin IProtocolRoster IProtocolMessaging IProtocolCapabilities IProtocolContactActions IProtocolPresence)

public:
    MeshCorePlugin();
    explicit MeshCorePlugin(MeshCoreProtocol *protocol);
    ~MeshCorePlugin();

    // IPlugin interface
    QObject *instance() override;
    QUuid pluginUuid() const override;
    void pluginInfo(IPluginInfo *APluginInfo) override;
    bool initConnections(IPluginManager *APluginManager, int &AInitOrder) override;
    bool initObjects() override;
    bool initSettings() override;
    bool startPlugin() override;

    // IProtocolRoster interface
    QString accountId() const override;
    ProtocolAccountIdentifier accountIdentifier() const override;
    IProtocolCapabilities::Capabilities capabilitiesForAccount(const AccountId &accountId,
        const ConversationId &targetId = ConversationId()) const override;
    bool showAddContactDialog(const AccountId &accountId) override;
    QString protocol() const override;
    QString streamId() const override;
    QList<ProtocolRosterEntry> entries() const override;
    QList<ProtocolRoom> rooms() const override;
    ProtocolRosterEntry entry(const QString &AId) const override;
    ProtocolRoom room(const QString &AId) const override;

    // IProtocolMessaging interface
    bool conversationIdForAddress(const Jid &address, ConversationId &conversationId) const override;
    Jid addressForConversation(const ConversationId &conversationId) const override;
    QString conversationDisplayName(const ConversationId &conversationId) const override;
    bool sendMessage(const BasicMessage &message) override;
    QList<BasicMessage> conversationHistory(const ConversationId &conversationId) const override;

    // IProtocolPresence interface; instance(), accountId(), and streamId() are
    // shared with the interfaces declared above.
    int show() const override;
    QString status() const override;
    bool setPresence(int AShow, const QString &AStatus) override;

private slots:
    void onProtocolMessageReceived(const BasicMessage &message);
    void onAccountAppended(IAccount *AAccount);
    void onAccountShown(IAccount *AAccount);
    void onAccountHidden(IAccount *AAccount);
    void onChannelsChanged();
    void attemptReconnect();

signals:
    void protocolRosterChanged();
    void protocolMessageReceived(const BasicMessage &message);
    void protocolPresenceChanged(const QString &streamId, int show, const QString &status);
    void protocolPresenceClosed(const QString &streamId);

private:
    void activateAccount(IAccount *account);
    void bindProtocolSignals();
    void onContactsChanged();
    void scheduleReconnect();
    void showJoinChatDialog(const QString &boundAccountId);

    IPluginManager *FPluginManager;
    IOptionsManager *FOptionsManager;
    IRostersViewPlugin *FRostersViewPlugin;
    IAccountManager *FAccountManager;
    IMessageWidgets *FMessageWidgets;
    IAccount *FSelectedAccount;
    bool FConnectRequested;
    int FPresenceShow;
    QString FPresenceStatus;
    int FRequestedShow;
    QString FRequestedStatus;
    MeshCoreProtocol *FProtocol;
    QTimer *FReconnectTimer;
    int FReconnectDelayMs;
};

#endif // MESHCOREPLUGIN_H
