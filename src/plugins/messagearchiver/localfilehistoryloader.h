#ifndef LOCALFILEHISTORYLOADER_H
#define LOCALFILEHISTORYLOADER_H

#include <QHash>
#include <QPointer>
#include <QSet>
#include <QObject>

#include <interfaces/identity.h>
#include <interfaces/imessage.h>
#include <interfaces/ifilemessagearchive.h>

class XmppLocalFileHistoryLoader : public QObject
{
    Q_OBJECT
public:
    explicit XmppLocalFileHistoryLoader(IFileMessageArchive *archive, QObject *parent = nullptr);

    bool requestHistory(const AccountId &accountId, const Jid &streamJid,
        const ConversationId &conversationId);

signals:
    void historyLoaded(const AccountId &accountId, const ConversationId &conversationId, const QList<BasicMessage> &messages);
    void historyLoadFailed(const AccountId &accountId, const ConversationId &conversationId, const QString &error);

private slots:
    void onHeadersLoaded(const QString &requestId, const QList<IArchiveHeader> &headers);
    void onCollectionLoaded(const QString &requestId, const IArchiveCollection &collection);
    void onRequestFailed(const QString &requestId, const XmppError &error);

private:
    struct PendingLoad
    {
        AccountId accountId;
        ConversationId conversationId;
        Jid streamJid;
        int pendingCollections = 0;
        QList<BasicMessage> messages;
    };

    IFileMessageArchive *archive() const;
    void complete(const QString &historyKey);
    void fail(const QString &historyKey, const QString &error);

    QPointer<QObject> FArchiveObject;
    QHash<QString, PendingLoad> FPendingLoads;
    QHash<QString, QString> FHeaderRequests;
    QHash<QString, QString> FCollectionRequests;
};

#endif // LOCALFILEHISTORYLOADER_H
