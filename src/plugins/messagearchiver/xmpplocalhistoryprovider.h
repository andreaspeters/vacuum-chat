#ifndef XMPPLOCALHISTORYPROVIDER_H
#define XMPPLOCALHISTORYPROVIDER_H

#include <QMap>
#include <QObject>
#include <QPointer>

#include <interfaces/iaccountmanager.h>
#include <interfaces/iprotocolmessaging.h>
#include <interfaces/iprotocolmessagingregistry.h>
#include <interfaces/ifilemessagearchive.h>
#include "localfilehistoryloader.h"

class XmppLocalHistoryProvider : public QObject, public IProtocolMessaging
{
    Q_OBJECT
    Q_INTERFACES(IProtocolMessaging)
public:
    XmppLocalHistoryProvider(const AccountId &accountId, const Jid &streamJid,
        IFileMessageArchive *archive, QObject *parent = nullptr);

    AccountId streamId() const override;
    QString protocol() const override;
    bool conversationIdForAddress(const Jid &address,
        ConversationId &conversationId) const override;
    Jid addressForConversation(const ConversationId &conversationId) const override;
    bool sendMessage(const BasicMessage &message) override;
    bool providesHistoryLoadedSignals() const override;
    QList<BasicMessage> conversationHistory(const ConversationId &conversationId) const override;
    bool requestConversationHistorySync(const ConversationId &conversationId) const override;
    Jid streamJid() const;

signals:
    void protocolMessageReceived(BasicMessage message);
    void protocolHistoryLoaded(QString conversationId);
    void localHistoryLoadFailed(QString conversationId, QString error);

private slots:
    void onHistoryLoaded(const AccountId &accountId, const ConversationId &conversationId,
        const QList<BasicMessage> &messages);
    void onHistoryLoadFailed(const AccountId &accountId, const ConversationId &conversationId,
        const QString &error);

private:
    AccountId FAccountId;
    Jid FStreamJid;
    XmppLocalFileHistoryLoader *FHistoryLoader;
    QMap<ConversationId, QList<BasicMessage>> FHistoryByConversation;
};

class XmppLocalHistoryProviderRegistry : public QObject, public IProtocolMessagingRegistry
{
    Q_OBJECT
    Q_INTERFACES(IProtocolMessagingRegistry)
public:
    XmppLocalHistoryProviderRegistry(IAccountManager *accountManager,
        IFileMessageArchive *archive, QObject *parent = nullptr);

    IProtocolMessaging *providerForAccount(const AccountId &accountId) override;

private:
    IAccountManager *FAccountManager;
    QPointer<QObject> FArchiveObject;
    QMap<AccountId, QPointer<XmppLocalHistoryProvider>> FProviders;
};

#endif // XMPPLOCALHISTORYPROVIDER_H
