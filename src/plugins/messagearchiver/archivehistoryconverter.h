#ifndef ARCHIVEHISTORYCONVERTER_H
#define ARCHIVEHISTORYCONVERTER_H

#include <interfaces/identity.h>
#include <interfaces/imessage.h>
#include <interfaces/imessagearchiver.h>

#include <QList>
#include <QString>

namespace XmppArchiveHistory
{
IArchiveRequest localFileArchiveRequest(const ConversationId &conversationId);
QList<BasicMessage> convert(const IArchiveCollection &collection,
    const ConversationId &conversationId, const QString &protocol);
}

#endif // ARCHIVEHISTORYCONVERTER_H
