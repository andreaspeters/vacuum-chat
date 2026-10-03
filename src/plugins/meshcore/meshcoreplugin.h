#ifndef MESHCOREPLUGIN_H
#define MESHCOREPLUGIN_H

#include <QObject>
#include <interfaces/ipluginmanager.h>
#include <interfaces/ioptionsmanager.h>
#include <interfaces/irostersview.h>
#include <interfaces/iprotocolroster.h>
#include <interfaces/iprotocolmessaging.h>
#include <interfaces/iaccountmanager.h>
#include <interfaces/iprotocolaccount.h>

class IAccount;
class IProtocolRoster;
class IMessageWidgets;
class MeshCoreProtocol;
class QTimer;

class MeshCorePlugin : public QObject, public IPlugin, public IProtocolRoster, public IProtocolMessaging
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "Vacuum.Core.IPlugin/1.0" FILE "meshcore.json")
    Q_INTERFACES(IPlugin IProtocolRoster IProtocolMessaging)

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

private slots:
    void onProtocolMessageReceived(const BasicMessage &message);
    void onAccountAppended(IAccount *AAccount);
    void onAccountShown(IAccount *AAccount);
    void onAccountHidden(IAccount *AAccount);
    void onChannelsChanged();
    void attemptReconnect();
    void onRostersViewIndexContextMenu(const QList<IRosterIndex *> &indexes,
                                       quint32 labelId, Menu *menu);

signals:
    void protocolRosterChanged();
    void protocolMessageReceived(const BasicMessage &message);

private:
    void activateAccount(IAccount *account);
    void onContactsChanged();
    void scheduleReconnect();
    void showJoinChatDialog(const QString &boundAccountId);

    IPluginManager *FPluginManager;
    IOptionsManager *FOptionsManager;
    IRostersViewPlugin *FRostersViewPlugin;
    IAccountManager *FAccountManager;
    IMessageWidgets *FMessageWidgets;
    IAccount *FSelectedAccount;
    MeshCoreProtocol *FProtocol;
    QTimer *FReconnectTimer;
    int FReconnectDelayMs;
};

#endif // MESHCOREPLUGIN_H
