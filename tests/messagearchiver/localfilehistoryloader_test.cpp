#include "localfilehistoryloader.h"
#include "xmpplocalhistoryprovider.h"

#include <QCoreApplication>
#include <iostream>

#include <interfaces/ifilemessagearchive.h>
#include <interfaces/iaccountmanager.h>
#include <definitions/messagedataroles.h>
#include <interfaces/imessageprocessor.h>

namespace
{
bool check(bool condition, const char *description)
{
    if (!condition)
        std::cerr << "FAIL: " << description << '\n';
    return condition;
}

class FakeFileMessageArchive : public QObject, public IFileMessageArchive
{
    Q_OBJECT
    Q_INTERFACES(IFileMessageArchive)
public:
    QObject *instance() override { return this; }
    QUuid engineId() const override { return FILEMESSAGEARCHIVE_UUID; }
    QString engineName() const override { return QStringLiteral("fake local archive"); }
    QString engineDescription() const override { return QString(); }
    IOptionsWidget *engineSettingsWidget(QWidget *) override { return nullptr; }
    quint32 capabilities(const Jid & = Jid::null) const override { return ArchiveManagement; }
    bool isCapable(const Jid &, quint32) const override { return true; }
    int capabilityOrder(quint32, const Jid & = Jid::null) const override { return 0; }
    bool saveMessage(const Jid &, const Message &, bool) override { return false; }
    bool saveNote(const Jid &, const Message &, bool) override { return false; }
    QString saveCollection(const Jid &, const IArchiveCollection &) override { return QString(); }
    QString loadHeaders(const Jid &streamJid, const IArchiveRequest &request) override
    {
        ++headerRequests;
        requestedStreams.append(streamJid);
        requestedRequests.append(request);
        return QStringLiteral("headers-%1").arg(headerRequests);
    }
    QString loadCollection(const Jid &streamJid, const IArchiveHeader &header) override
    {
        requestedHeaders.append(header);
        requestedCollectionStreams.append(streamJid);
        return QStringLiteral("collection-%1").arg(requestedHeaders.size());
    }
    QString removeCollections(const Jid &, const IArchiveRequest &) override { return QString(); }
    QString loadModifications(const Jid &, const QDateTime &, int) override { return QString(); }
    QString archiveHomePath() const override { return QString(); }
    QString collectionDirName(const Jid &) const override { return QString(); }
    QString collectionFileName(const QDateTime &) const override { return QString(); }
    QString collectionDirPath(const Jid &, const Jid &) const override { return QString(); }
    QString collectionFilePath(const Jid &, const Jid &, const QDateTime &) const override { return QString(); }
    QStringList findCollectionFiles(const Jid &, const IArchiveRequest &) const override { return {}; }
    IArchiveHeader loadHeaderFromFile(const QString &) const override { return {}; }
    IArchiveCollection loadCollectionFromFile(const QString &) const override { return {}; }
    IArchiveModifications loadFileModifications(const Jid &, const QDateTime &, int) const override { return {}; }
    bool saveCollectionToFile(const Jid &, const IArchiveCollection &, const QString &, bool) override { return false; }
    bool removeCollectionFile(const Jid &, const Jid &, const QDateTime &) override { return false; }

    void deliverHeaders(const QString &requestId, const QList<IArchiveHeader> &headers)
    { emit headersLoaded(requestId, headers); }
    void deliverCollection(const QString &requestId, const IArchiveCollection &collection)
    { emit collectionLoaded(requestId, collection); }
    void failRequest(const QString &requestId)
    { emit requestFailed(requestId, XmppError(QStringLiteral("service-unavailable"),
        QStringLiteral("synthetic archive failure"))); }

    int headerRequests = 0;
    QList<Jid> requestedStreams;
    QList<IArchiveRequest> requestedRequests;
    QList<IArchiveHeader> requestedHeaders;
    QList<Jid> requestedCollectionStreams;

signals:
    void capabilitiesChanged(const Jid &streamJid);
    void headersLoaded(const QString &requestId, const QList<IArchiveHeader> &headers);
    void collectionLoaded(const QString &requestId, const IArchiveCollection &collection);
    void requestFailed(const QString &requestId, const XmppError &error);
    void collectionSaved(const QString &requestId, const IArchiveHeader &header);
    void collectionsRemoved(const QString &requestId, const IArchiveRequest &request);
    void modificationsLoaded(const QString &requestId, const IArchiveModifications &modifications);

protected:
    void fileCollectionOpened(const Jid &, const IArchiveHeader &) override {}
    void fileCollectionSaved(const Jid &, const IArchiveHeader &) override {}
    void fileCollectionRemoved(const Jid &, const IArchiveHeader &) override {}
};

class FakeAccount : public IAccount
{
public:
    FakeAccount(const QUuid &id, ProtocolKind kind, const Jid &streamJid) :
        FId(id), FKind(kind), FStreamJid(streamJid) {}
    QObject *instance() override { return nullptr; }
    ProtocolKind protocolKind() const override { return FKind; }
    Capabilities capabilities() const override { return CapabilityChat; }
    ConnectionState connectionState() const override { return StateDisconnected; }
    QUuid accountId() const override { return FId; }
    bool isActive() const override { return false; }
    QString name() const override { return QString(); }
    OptionsNode optionsNode() const override { return OptionsNode(); }
    bool isValid() const override { return true; }
    void setActive(bool) override {}
    void setName(const QString &) override {}
    Jid streamJid() const override { return FStreamJid; }
    void setStreamJid(const Jid &jid) override { FStreamJid = jid; }
    QString password() const override { return QString(); }
    void setPassword(const QString &) override {}
    IXmppStream *xmppStream() const override { return nullptr; }
protected:
    void activeChanged(bool) override {}
    void optionsChanged(const OptionsNode &) override {}
private:
    QUuid FId;
    ProtocolKind FKind;
    Jid FStreamJid;
};

class FakeAccountManager : public IAccountManager
{
public:
    explicit FakeAccountManager(const QList<IAccount *> &accounts) : FAccounts(accounts) {}
    QObject *instance() override { return nullptr; }
    QList<IAccount *> accounts() const override { return FAccounts; }
    IAccount *accountById(const QUuid &id) const override
    {
        for (IAccount *account : FAccounts)
            if (account && account->accountId() == id)
                return account;
        return nullptr;
    }
    IAccount *accountByProtocolId(const AccountId &id) const override
    { return accountById(QUuid::fromString(id)); }
    IAccount *accountByStream(const Jid &jid) const override
    {
        for (IAccount *account : FAccounts)
            if (account && account->streamJid() == jid)
                return account;
        return nullptr;
    }
    IAccount *appendAccount(const QUuid &) override { return nullptr; }
    void showAccount(const QUuid &) override {}
    void hideAccount(const QUuid &) override {}
    void removeAccount(const QUuid &) override {}
    void destroyAccount(const QUuid &) override {}
protected:
    void appended(IAccount *) override {}
    void shown(IAccount *) override {}
    void hidden(IAccount *) override {}
    void removed(IAccount *) override {}
    void changed(IAccount *, const OptionsNode &) override {}
    void destroyed(const QUuid &) override {}
private:
    QList<IAccount *> FAccounts;
};
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    FakeFileMessageArchive archive;
    XmppLocalFileHistoryLoader loader(&archive);
    const Jid streamJid(QStringLiteral("me@example.org/resource"));
    const ConversationId conversationId = QStringLiteral("peer@example.org");
    QMap<AccountId, QList<BasicMessage>> loadedMessages;
    QObject::connect(&loader, &XmppLocalFileHistoryLoader::historyLoaded,
        [&loadedMessages](const AccountId &loadedAccount, const ConversationId &loadedConversation,
            const QList<BasicMessage> &messages) {
            if (loadedConversation == QStringLiteral("peer@example.org"))
                loadedMessages.insert(loadedAccount, messages);
        });

    const AccountId accountA = QStringLiteral("xmpp-account-a");
    const AccountId accountB = QStringLiteral("xmpp-account-b");
    const Jid streamJidA(QStringLiteral("me@example.org/resource-a"));
    const Jid streamJidB(QStringLiteral("me@example.org/resource-b"));
    if (!check(loader.requestHistory(accountA, streamJidA, conversationId),
            "valid local conversation history request should start") ||
        !check(loader.requestHistory(accountB, streamJidB, conversationId),
            "the same peer conversation should load independently for another account") ||
        !check(!loader.requestHistory(accountA, streamJidA, conversationId),
            "duplicate in-flight load should be rejected for the same account only") ||
        !check(archive.headerRequests == 2 && archive.requestedStreams.at(0) == streamJidA &&
            archive.requestedStreams.at(1) == streamJidB,
            "history headers must be requested from the local file archive for each account stream") ||
        !check(archive.requestedRequests.at(0).with.full() == conversationId &&
            !archive.requestedRequests.at(0).exactmatch &&
            archive.requestedRequests.at(0).order == Qt::AscendingOrder,
            "history request must retain bare-JID local archive scope and chronological order"))
        return 1;

    IArchiveHeader header;
    header.with = Jid(conversationId);
    header.start = QDateTime::fromString(QStringLiteral("2025-03-04T10:00:00Z"), Qt::ISODate);
    archive.deliverHeaders(QStringLiteral("headers-1"), {header});
    archive.deliverHeaders(QStringLiteral("headers-2"), {header});
    if (!check(archive.requestedHeaders.size() == 2 &&
            archive.requestedCollectionStreams.at(0) == streamJidA &&
            archive.requestedCollectionStreams.at(1) == streamJidB,
            "each local archive header should be loaded asynchronously"))
        return 1;

    IArchiveCollection collectionA;
    collectionA.header = header;
    Message messageA;
    messageA.setFrom(conversationId).setTo(streamJidA.full())
        .setBody(QStringLiteral("persisted locally"))
        .setDateTime(QDateTime::fromString(QStringLiteral("2025-03-04T10:01:00Z"), Qt::ISODate));
    messageA.setData(MDR_MESSAGE_DIRECTION, IMessageProcessor::MessageIn);
    collectionA.body.messages.append(messageA);
    IArchiveCollection collectionB;
    collectionB.header = header;
    Message messageB;
    messageB.setFrom(conversationId).setTo(streamJidB.full())
        .setBody(QStringLiteral("other account history"))
        .setDateTime(QDateTime::fromString(QStringLiteral("2025-03-04T10:02:00Z"), Qt::ISODate));
    messageB.setData(MDR_MESSAGE_DIRECTION, IMessageProcessor::MessageIn);
    collectionB.body.messages.append(messageB);
    archive.deliverCollection(QStringLiteral("collection-2"), collectionB);
    archive.deliverCollection(QStringLiteral("collection-1"), collectionA);

    if (!check(loadedMessages.value(accountA).size() == 1 &&
            loadedMessages.value(accountA).constFirst().body() == QStringLiteral("persisted locally") &&
            loadedMessages.value(accountA).constFirst().conversationId() == conversationId &&
            loadedMessages.value(accountA).constFirst().protocol() == QStringLiteral("xmpp") &&
            loadedMessages.value(accountB).size() == 1 &&
            loadedMessages.value(accountB).constFirst().body() == QStringLiteral("other account history"),
        "local history responses must remain isolated by AccountId and ConversationId"))
        return 1;

    XmppLocalHistoryProvider provider(accountA, streamJidA, &archive);
    ConversationId mappedConversation;
    QList<BasicMessage> providerMessages;
    bool providerHistoryLoaded = false;
    bool failedProviderHistoryLoaded = false;
    bool providerHistoryFailed = false;
    QObject::connect(&provider, &XmppLocalHistoryProvider::protocolMessageReceived,
        [&providerMessages](const BasicMessage &message) { providerMessages.append(message); });
    QObject::connect(&provider, &XmppLocalHistoryProvider::protocolHistoryLoaded,
        [&providerHistoryLoaded](const QString &loadedConversation) {
            providerHistoryLoaded = loadedConversation == QStringLiteral("peer@example.org");
        });
    QObject::connect(&provider, &XmppLocalHistoryProvider::protocolHistoryLoaded,
        [&failedProviderHistoryLoaded](const QString &loadedConversation) {
            failedProviderHistoryLoaded = loadedConversation ==
                QStringLiteral("failed-peer@example.org");
        });
    QObject::connect(&provider, &XmppLocalHistoryProvider::localHistoryLoadFailed,
        [&providerHistoryFailed](const QString &conversation, const QString &error) {
            providerHistoryFailed = conversation == QStringLiteral("failed-peer@example.org") &&
                !error.isEmpty();
        });
    if (!check(provider.streamId() == accountA && provider.protocol() == QStringLiteral("xmpp"),
            "the XMPP provider must expose the persistent account identity and protocol") ||
        !check(provider.conversationIdForAddress(Jid(conversationId), mappedConversation) &&
            mappedConversation == conversationId &&
            provider.addressForConversation(conversationId).pBare() == conversationId,
            "the XMPP provider must map JIDs to stable conversation IDs") ||
        !check(provider.requestConversationHistorySync(conversationId),
            "the shared messaging history API must start an asynchronous local archive read") ||
        !check(!provider.requestConversationHistorySync(conversationId),
            "the same account/conversation must not start a duplicate in-flight read"))
        return 1;

    archive.deliverHeaders(QStringLiteral("headers-3"), {header});
    IArchiveCollection providerCollection;
    providerCollection.header = header;
    Message providerMessage;
    providerMessage.setFrom(conversationId).setTo(streamJidA.full())
        .setBody(QStringLiteral("provider history"))
        .setDateTime(QDateTime::fromString(QStringLiteral("2025-03-04T10:03:00Z"), Qt::ISODate));
    providerMessage.setData(MDR_MESSAGE_DIRECTION, IMessageProcessor::MessageIn);
    providerCollection.body.messages.append(providerMessage);
    archive.deliverCollection(QStringLiteral("collection-3"), providerCollection);

    const QList<BasicMessage> cachedProviderHistory = provider.conversationHistory(conversationId);
    if (!check(providerHistoryLoaded && providerMessages.size() == 1 &&
            cachedProviderHistory.size() == 1 &&
            providerMessages.constFirst().body() == QStringLiteral("provider history") &&
            providerMessages.constFirst().metadata().value(QStringLiteral("historical")).toBool() &&
            cachedProviderHistory.constFirst().protocol() == QStringLiteral("xmpp"),
        "local archive results must flow through the shared asynchronous messaging-history API"))
        return 1;

    if (!check(provider.requestConversationHistorySync(QStringLiteral("failed-peer@example.org")),
            "a new local history request can start after the previous request completes"))
        return 1;
    archive.failRequest(QStringLiteral("headers-4"));
    if (!check(providerHistoryFailed && failedProviderHistoryLoaded,
            "a failed asynchronous history request must still complete the shared history load"))
        return 1;

    FakeAccount registryAccountA(QUuid(QStringLiteral("{11111111-2222-4333-8444-555555555555}")),
        IProtocolAccount::ProtocolXmpp, streamJidA);
    FakeAccount registryAccountB(QUuid(QStringLiteral("{22222222-3333-4444-8555-666666666666}")),
        IProtocolAccount::ProtocolXmpp, streamJidB);
    FakeAccount nonXmppAccount(QUuid(QStringLiteral("{33333333-4444-4555-8666-777777777777}")),
        IProtocolAccount::ProtocolMatrix, Jid(QStringLiteral("@alice:matrix.example")));
    FakeAccountManager accountManager({&registryAccountA, &registryAccountB, &nonXmppAccount});
    XmppLocalHistoryProviderRegistry registry(&accountManager, &archive);
    const AccountId registryAccountIdA = registryAccountA.accountId().toString(QUuid::WithoutBraces);
    const AccountId registryAccountIdB = registryAccountB.accountId().toString(QUuid::WithoutBraces);
    IProtocolMessaging *registryProviderA = registry.providerForAccount(registryAccountIdA);
    IProtocolMessaging *registryProviderB = registry.providerForAccount(registryAccountIdB);
    return check(registryProviderA && registryProviderB && registryProviderA != registryProviderB &&
            registry.providerForAccount(registryAccountIdA) == registryProviderA &&
            registryProviderA->streamId() == registryAccountIdA &&
            registryProviderB->streamId() == registryAccountIdB &&
            !registry.providerForAccount(nonXmppAccount.accountId().toString(QUuid::WithoutBraces)) &&
            !registry.providerForAccount(QStringLiteral("not-a-uuid")),
        "the history registry must cache separate XMPP providers per persistent account and reject other protocols") ? 0 : 1;
}

#include "localfilehistoryloader_test.moc"
