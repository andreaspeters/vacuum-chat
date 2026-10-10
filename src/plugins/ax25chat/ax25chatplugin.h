#ifndef AX25CHATPLUGIN_H
#define AX25CHATPLUGIN_H

#include <interfaces/ipluginmanager.h>
#include <interfaces/iprotocolcapabilities.h>
#include <interfaces/iprotocolcontactactions.h>
#include <interfaces/iprotocolmessaging.h>
#include <interfaces/iprotocolpresence.h>
#include <interfaces/iprotocolroster.h>

#include <QElapsedTimer>
#include <QObject>
#include <QStringList>

class IAccount;
class IAccountManager;
class IPluginManager;
class QTimer;
class Ax25KissSerialTransport;

namespace Axcp
{
class ReliableEngine;
}

class Ax25ChatPlugin : public QObject,
                       public IPlugin,
                       public IProtocolMessaging,
                       public IProtocolRoster,
                       public IProtocolPresence,
                       public IProtocolCapabilities,
                       public IProtocolContactActions
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "Vacuum.Core.IPlugin/1.0" FILE "ax25chat.json")
    Q_INTERFACES(IPlugin IProtocolMessaging IProtocolRoster IProtocolPresence
                 IProtocolCapabilities IProtocolContactActions)

public:
    explicit Ax25ChatPlugin(QObject *parent = nullptr);
    ~Ax25ChatPlugin() override;

    QObject *instance() override;
    QUuid pluginUuid() const override;
    void pluginInfo(IPluginInfo *pluginInfo) override;
    bool initConnections(IPluginManager *pluginManager, int &initOrder) override;
    bool initObjects() override;
    bool initSettings() override;
    bool startPlugin() override;

    QString accountId() const override;
    ProtocolAccountIdentifier accountIdentifier() const override;
    QString protocol() const override;
    QString streamId() const override;
    QList<ProtocolRosterEntry> entries() const override;
    QList<ProtocolRoom> rooms() const override;
    ProtocolRosterEntry entry(const QString &id) const override;
    ProtocolRoom room(const QString &id) const override;

    bool conversationIdForAddress(const Jid &address,
                                  ConversationId &conversationId) const override;
    Jid addressForConversation(const ConversationId &conversationId) const override;
    QString conversationDisplayName(const ConversationId &conversationId) const override;
    bool sendMessage(const BasicMessage &message) override;

    int show() const override;
    QString status() const override;
    bool setPresence(int show, const QString &status) override;

    IProtocolCapabilities::Capabilities capabilitiesForAccount(
        const AccountId &accountId,
        const ConversationId &targetId = ConversationId()) const override;
    bool showAddContactDialog(const AccountId &accountId) override;

signals:
    void protocolRosterChanged();
    void protocolMessageReceived(const BasicMessage &message);
    void protocolPresenceChanged(const QString &streamId, int show, const QString &status);
    void protocolPresenceClosed(const QString &streamId);

private slots:
    void onAccountAppended(IAccount *account);
    void onAccountShown(IAccount *account);
    void onAccountHidden(IAccount *account);
    void onIncomingMessage(const QString &source, quint32 messageId, const QString &text);
    void onTransportDisconnected();
    void onTransportError(const QString &error);
    void onRetryTimer();

private:
    void activateAccount(IAccount *account);
    void resetReliableEngine();
    void updateOfflinePresence();
    bool isSelectedAccount(const AccountId &accountId) const;
    QString settingsFilePath() const;
    QStringList contactCallsigns() const;
    bool addContactCallsign(const QString &callsign);
    static QString canonicalCallsign(const QString &callsign);

    IPluginManager *m_pluginManager = nullptr;
    IAccountManager *m_accountManager = nullptr;
    IAccount *m_selectedAccount = nullptr;
    Ax25KissSerialTransport *m_transport = nullptr;
    Axcp::ReliableEngine *m_engine = nullptr;
    QTimer *m_retryTimer = nullptr;
    QElapsedTimer m_clock;
    int m_show = 5;
    QString m_status;
};

#endif // AX25CHATPLUGIN_H
