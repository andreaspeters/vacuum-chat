#include "archivehistoryconverter.h"

#include <QCryptographicHash>
#include <QByteArray>
#include <definitions/messagedataroles.h>
#include <interfaces/imessageprocessor.h>

namespace
{
void appendHashField(QByteArray &payload, const QString &value)
{
    const QByteArray bytes = value.toUtf8();
    payload.append(QByteArray::number(bytes.size()));
    payload.append(':');
    payload.append(bytes);
}
}

namespace XmppArchiveHistory
{
IArchiveRequest localFileArchiveRequest(const ConversationId &conversationId)
{
    IArchiveRequest request;
    const Jid with(conversationId);
    if (with.isValid()) {
        request.with = with;
        request.exactmatch = !with.resource().isEmpty();
    }
    return request;
}

QList<BasicMessage> convert(const IArchiveCollection &collection,
    const ConversationId &conversationId, const QString &protocol)
{
    QList<BasicMessage> result;
    if (conversationId.isEmpty() || protocol.isEmpty())
        return result;

    result.reserve(collection.body.messages.size());
    for (const Message &message : collection.body.messages) {
        bool directionValid = false;
        const int archivedDirection = message.data(MDR_MESSAGE_DIRECTION).toInt(&directionValid);
        BasicMessage::Direction direction = BasicMessage::System;
        if (directionValid && archivedDirection == IMessageProcessor::MessageIn)
            direction = BasicMessage::Incoming;
        else if (directionValid && archivedDirection == IMessageProcessor::MessageOut)
            direction = BasicMessage::Outgoing;

        const QString timestamp = message.dateTime().toUTC().toString(Qt::ISODateWithMs);
        const QString sessionStart = collection.header.start.toUTC().toString(Qt::ISODateWithMs);
        QByteArray idPayload;
        appendHashField(idPayload, protocol);
        appendHashField(idPayload, conversationId);
        appendHashField(idPayload, collection.header.with.full());
        appendHashField(idPayload, sessionStart);
        appendHashField(idPayload, timestamp);
        appendHashField(idPayload, QString::number(static_cast<int>(direction)));
        appendHashField(idPayload, message.from());
        appendHashField(idPayload, message.to());
        appendHashField(idPayload, message.body());
        appendHashField(idPayload, message.threadId());
        const QString messageId = QStringLiteral("xmpp-archive:") +
            QString::fromLatin1(QCryptographicHash::hash(
                idPayload, QCryptographicHash::Sha256).toHex());

        BasicMessage converted(messageId, conversationId, message.from(), message.to(),
            message.body(), message.dateTime(), protocol, direction);
        QVariantMap metadata;
        if (!message.threadId().isEmpty())
            metadata.insert(QStringLiteral("thread_id"), message.threadId());
        if (!collection.header.subject.isEmpty())
            metadata.insert(QStringLiteral("subject"), collection.header.subject);
        converted.setMetadata(metadata);
        result.append(converted);
    }
    return result;
}
}
