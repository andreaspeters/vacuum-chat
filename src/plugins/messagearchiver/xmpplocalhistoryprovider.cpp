#include "xmpplocalhistoryprovider.h"

XmppLocalHistoryProvider::XmppLocalHistoryProvider(const AccountId &accountId,
    const Jid &streamJid, IFileMessageArchive *archive, QObject *parent) : QObject(parent),
    FAccountId(accountId), FStreamJid(streamJid), FHistoryLoader(new XmppLocalFileHistoryLoader(archive, this))
{
    connect(FHistoryLoader, &XmppLocalFileHistoryLoader::historyLoaded,
        this, &XmppLocalHistoryProvider::onHistoryLoaded);
    connect(FHistoryLoader, &XmppLocalFileHistoryLoader::historyLoadFailed,
        this, &XmppLocalHistoryProvider::onHistoryLoadFailed);
}

AccountId XmppLocalHistoryProvider::streamId() const
{
    return FAccountId;
}

QString XmppLocalHistoryProvider::protocol() const
{
    return QStringLiteral("xmpp");
}

bool XmppLocalHistoryProvider::conversationIdForAddress(const Jid &address,
    ConversationId &conversationId) const
{
    if (!address.isValid())
        return false;
    conversationId = address.pBare();
    return !conversationId.isEmpty();
}

Jid XmppLocalHistoryProvider::addressForConversation(const ConversationId &conversationId) const
{
    return Jid(conversationId);
}

bool XmppLocalHistoryProvider::sendMessage(const BasicMessage &message)
{
    Q_UNUSED(message);
    return false;
}

bool XmppLocalHistoryProvider::providesHistoryLoadedSignals() const
{
    return true;
}

QList<BasicMessage> XmppLocalHistoryProvider::conversationHistory(
    const ConversationId &conversationId) const
{
    return FHistoryByConversation.value(conversationId);
}

bool XmppLocalHistoryProvider::requestConversationHistorySync(
    const ConversationId &conversationId) const
{
    return FHistoryLoader && !FAccountId.isEmpty() && FStreamJid.isValid() &&
        !conversationId.isEmpty() &&
        FHistoryLoader->requestHistory(FAccountId, FStreamJid, conversationId);
}

Jid XmppLocalHistoryProvider::streamJid() const
{
	return FStreamJid;
}

void XmppLocalHistoryProvider::onHistoryLoaded(const AccountId &accountId,
    const ConversationId &conversationId, const QList<BasicMessage> &messages)
{
    if (accountId != FAccountId || conversationId.isEmpty())
        return;

    QList<BasicMessage> historicalMessages;
    historicalMessages.reserve(messages.size());
    for (BasicMessage message : messages) {
        QVariantMap metadata = message.metadata();
        metadata.insert(QStringLiteral("historical"), true);
        message.setMetadata(metadata);
        historicalMessages.append(message);
    }
    FHistoryByConversation.insert(conversationId, historicalMessages);
    for (const BasicMessage &message : historicalMessages)
        emit protocolMessageReceived(message);
    emit protocolHistoryLoaded(conversationId);
}

void XmppLocalHistoryProvider::onHistoryLoadFailed(const AccountId &accountId,
    const ConversationId &conversationId, const QString &error)
{
    if (accountId == FAccountId && !conversationId.isEmpty()) {
        emit localHistoryLoadFailed(conversationId, error);
        emit protocolHistoryLoaded(conversationId);
    }
}

XmppLocalHistoryProviderRegistry::XmppLocalHistoryProviderRegistry(
    IAccountManager *accountManager, IFileMessageArchive *archive, QObject *parent) :
    QObject(parent), FAccountManager(accountManager),
    FArchiveObject(archive ? archive->instance() : nullptr)
{
}

IProtocolMessaging *XmppLocalHistoryProviderRegistry::providerForAccount(
    const AccountId &accountId)
{
    if (!FAccountManager || !FArchiveObject || accountId.isEmpty())
        return nullptr;

    const QUuid persistentAccountId = QUuid::fromString(accountId);
    if (persistentAccountId.isNull())
        return nullptr;

    IAccount *account = FAccountManager->accountById(persistentAccountId);
    if (!account || account->accountId() != persistentAccountId ||
        account->protocolKind() != IProtocolAccount::ProtocolXmpp)
        return nullptr;

    const Jid streamJid = account->streamJid();
    IFileMessageArchive *archive = qobject_cast<IFileMessageArchive *>(FArchiveObject.data());
    if (!streamJid.isValid() || !archive)
        return nullptr;

    QPointer<XmppLocalHistoryProvider> provider = FProviders.value(accountId);
    if (provider && provider->streamJid() == streamJid)
        return provider.data();

    if (provider) {
        FProviders.remove(accountId);
        delete provider.data();
    }

    provider = new XmppLocalHistoryProvider(accountId, streamJid, archive, this);
    FProviders.insert(accountId, provider);
    return provider.data();
}
