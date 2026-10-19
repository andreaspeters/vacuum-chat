#include "localfilehistoryloader.h"

#include "archivehistoryconverter.h"

#include <algorithm>

namespace
{
QString makeHistoryKey(const AccountId &accountId, const ConversationId &conversationId)
{
    return QString::number(accountId.size()) + QLatin1Char(':') + accountId + conversationId;
}
}

XmppLocalFileHistoryLoader::XmppLocalFileHistoryLoader(
    IFileMessageArchive *archive, QObject *parent) : QObject(parent)
{
    FArchiveObject = archive ? archive->instance() : nullptr;
    if (FArchiveObject) {
        connect(FArchiveObject, SIGNAL(headersLoaded(const QString &, const QList<IArchiveHeader> &)),
            this, SLOT(onHeadersLoaded(const QString &, const QList<IArchiveHeader> &)));
        connect(FArchiveObject, SIGNAL(collectionLoaded(const QString &, const IArchiveCollection &)),
            this, SLOT(onCollectionLoaded(const QString &, const IArchiveCollection &)));
        connect(FArchiveObject, SIGNAL(requestFailed(const QString &, const XmppError &)),
            this, SLOT(onRequestFailed(const QString &, const XmppError &)));
    }
}

IFileMessageArchive *XmppLocalFileHistoryLoader::archive() const
{
    return FArchiveObject ? qobject_cast<IFileMessageArchive *>(FArchiveObject.data()) : nullptr;
}

bool XmppLocalFileHistoryLoader::requestHistory(
    const AccountId &accountId, const Jid &streamJid, const ConversationId &conversationId)
{
    IFileMessageArchive *localArchive = archive();
    const IArchiveRequest request = XmppArchiveHistory::localFileArchiveRequest(conversationId);
    const QString historyKey = makeHistoryKey(accountId, conversationId);
    if (!localArchive || accountId.isEmpty() || !streamJid.isValid() ||
        !request.with.isValid() || FPendingLoads.contains(historyKey))
        return false;

    PendingLoad pending;
    pending.accountId = accountId;
    pending.conversationId = conversationId;
    pending.streamJid = streamJid;
    FPendingLoads.insert(historyKey, pending);
    const QString requestId = localArchive->loadHeaders(streamJid, request);
    if (requestId.isEmpty() || FHeaderRequests.contains(requestId)) {
        fail(historyKey, tr("Failed to start local history request"));
        return false;
    }
    FHeaderRequests.insert(requestId, historyKey);
    return true;
}

void XmppLocalFileHistoryLoader::onHeadersLoaded(
    const QString &requestId, const QList<IArchiveHeader> &headers)
{
    const QString historyKey = FHeaderRequests.take(requestId);
    if (historyKey.isEmpty() || !FPendingLoads.contains(historyKey))
        return;

    PendingLoad &pending = FPendingLoads[historyKey];
    IFileMessageArchive *localArchive = archive();
    if (!localArchive) {
        fail(historyKey, tr("Local archive is no longer available"));
        return;
    }

    for (const IArchiveHeader &header : headers) {
        const QString collectionRequestId = localArchive->loadCollection(pending.streamJid, header);
        if (collectionRequestId.isEmpty() || FCollectionRequests.contains(collectionRequestId)) {
            fail(historyKey, tr("Failed to start local archive collection request"));
            return;
        }
        ++pending.pendingCollections;
        FCollectionRequests.insert(collectionRequestId, historyKey);
    }

    if (pending.pendingCollections == 0)
        complete(historyKey);
}

void XmppLocalFileHistoryLoader::onCollectionLoaded(
    const QString &requestId, const IArchiveCollection &collection)
{
    const QString historyKey = FCollectionRequests.take(requestId);
    auto pendingIt = FPendingLoads.find(historyKey);
    if (historyKey.isEmpty() || pendingIt == FPendingLoads.end())
        return;

    pendingIt->messages.append(XmppArchiveHistory::convert(
        collection, pendingIt->conversationId, QStringLiteral("xmpp")));
    --pendingIt->pendingCollections;
    if (pendingIt->pendingCollections == 0)
        complete(historyKey);
}

void XmppLocalFileHistoryLoader::onRequestFailed(
    const QString &requestId, const XmppError &error)
{
    QString historyKey = FHeaderRequests.take(requestId);
    if (historyKey.isEmpty())
        historyKey = FCollectionRequests.take(requestId);
    if (!historyKey.isEmpty())
        fail(historyKey, error.errorMessage());
}

void XmppLocalFileHistoryLoader::complete(const QString &historyKey)
{
    auto pendingIt = FPendingLoads.find(historyKey);
    if (pendingIt == FPendingLoads.end())
        return;

    const AccountId accountId = pendingIt->accountId;
    const ConversationId conversationId = pendingIt->conversationId;
    QList<BasicMessage> messages = pendingIt->messages;
    FPendingLoads.erase(pendingIt);
    std::stable_sort(messages.begin(), messages.end(), [](const BasicMessage &left,
        const BasicMessage &right) {
        const QDateTime leftTime = left.timestamp();
        const QDateTime rightTime = right.timestamp();
        if (leftTime.isValid() != rightTime.isValid())
            return leftTime.isValid();
        if (leftTime.isValid() && leftTime != rightTime)
            return leftTime < rightTime;
        return left.messageId() < right.messageId();
    });

    QSet<QString> seenIds;
    QList<BasicMessage> uniqueMessages;
    uniqueMessages.reserve(messages.size());
    for (const BasicMessage &message : messages) {
        if (message.messageId().isEmpty() || !seenIds.contains(message.messageId())) {
            uniqueMessages.append(message);
            if (!message.messageId().isEmpty())
                seenIds.insert(message.messageId());
        }
    }
    emit historyLoaded(accountId, conversationId, uniqueMessages);
}

void XmppLocalFileHistoryLoader::fail(const QString &historyKey, const QString &error)
{
    auto pendingIt = FPendingLoads.find(historyKey);
    if (pendingIt == FPendingLoads.end())
        return;
    const AccountId accountId = pendingIt->accountId;
    const ConversationId conversationId = pendingIt->conversationId;
    FPendingLoads.erase(pendingIt);

    for (auto it = FHeaderRequests.begin(); it != FHeaderRequests.end();) {
        if (it.value() == historyKey)
            it = FHeaderRequests.erase(it);
        else
            ++it;
    }
    for (auto it = FCollectionRequests.begin(); it != FCollectionRequests.end();) {
        if (it.value() == historyKey)
            it = FCollectionRequests.erase(it);
        else
            ++it;
    }
    emit historyLoadFailed(accountId, conversationId, error);
}
