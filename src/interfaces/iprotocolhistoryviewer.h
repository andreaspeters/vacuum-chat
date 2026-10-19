#ifndef IPROTOCOLHISTORYVIEWER_H
#define IPROTOCOLHISTORYVIEWER_H

#include <interfaces/identity.h>
#include <interfaces/imessage.h>

#include <QList>
#include <QString>
#include <QtPlugin>

class QWidget;

class IProtocolHistoryViewer
{
public:
    virtual ~IProtocolHistoryViewer() {}
    virtual QWidget *showConversationHistory(const AccountId &accountId,
        const ConversationId &conversationId, const QString &conversationName,
        const QList<BasicMessage> &messages) = 0;
};

Q_DECLARE_INTERFACE(IProtocolHistoryViewer, "Vacuum.Plugin.IProtocolHistoryViewer/1.0")

#endif // IPROTOCOLHISTORYVIEWER_H
