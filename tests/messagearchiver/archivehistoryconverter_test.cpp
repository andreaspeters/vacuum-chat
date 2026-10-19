#include "archivehistoryconverter.h"

#include <QCoreApplication>
#include <QDateTime>
#include <iostream>

#include <definitions/messagedataroles.h>
#include <interfaces/imessageprocessor.h>
#include <interfaces/identity.h>

namespace
{
bool check(bool condition, const char *message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    IArchiveCollection collection;
    collection.header.with = Jid(QStringLiteral("peer@example.org"));
    collection.header.start = QDateTime::fromString(
        QStringLiteral("2025-03-04T10:00:00Z"), Qt::ISODate);

    const QDateTime incomingTime = QDateTime::fromString(
        QStringLiteral("2025-03-04T10:01:00Z"), Qt::ISODate);
    Message incoming;
    incoming.setFrom(QStringLiteral("peer@example.org"))
        .setTo(QStringLiteral("me@example.org"))
        .setBody(QStringLiteral("hello from peer"))
        .setDateTime(incomingTime)
        .setThreadId(QStringLiteral("thread-7"));
    incoming.setData(MDR_MESSAGE_DIRECTION, IMessageProcessor::MessageIn);

    const QDateTime outgoingTime = QDateTime::fromString(
        QStringLiteral("2025-03-04T10:02:00Z"), Qt::ISODate);
    Message outgoing;
    outgoing.setFrom(QStringLiteral("me@example.org"))
        .setTo(QStringLiteral("peer@example.org"))
        .setBody(QStringLiteral("hello back"))
        .setDateTime(outgoingTime);
    outgoing.setData(MDR_MESSAGE_DIRECTION, IMessageProcessor::MessageOut);

    collection.body.messages << incoming << outgoing;

    const ConversationId conversationId = QStringLiteral("peer@example.org");
    const IArchiveRequest bareRequest =
        XmppArchiveHistory::localFileArchiveRequest(conversationId);
    if (!check(bareRequest.with.full() == QStringLiteral("peer@example.org"),
            "local archive request should preserve a bare conversation JID") ||
        !check(!bareRequest.exactmatch,
            "bare JID requests should include resource subdirectories") ||
        !check(bareRequest.order == Qt::AscendingOrder && bareRequest.maxItems == 0,
            "local history request should load all messages in chronological order"))
        return 1;

    const ConversationId resourceConversationId = QStringLiteral("peer@example.org/mobile");
    const IArchiveRequest resourceRequest =
        XmppArchiveHistory::localFileArchiveRequest(resourceConversationId);
    if (!check(resourceRequest.with.full() == QStringLiteral("peer@example.org/mobile"),
            "local archive request should preserve the resource-qualified conversation JID") ||
        !check(resourceRequest.exactmatch,
            "resource-qualified conversation requests should stay isolated"))
        return 1;

    const IArchiveRequest invalidRequest = XmppArchiveHistory::localFileArchiveRequest(QString());
    if (!check(!invalidRequest.with.isValid(),
            "empty conversation identity should not create a local archive target"))
        return 1;

    const QList<BasicMessage> converted = XmppArchiveHistory::convert(
        collection, conversationId, QStringLiteral("xmpp"));
    if (!check(converted.size() == 2, "both archived messages should convert"))
        return 1;

    const BasicMessage &convertedIncoming = converted.at(0);
    if (!check(convertedIncoming.direction() == BasicMessage::Incoming,
            "archive incoming direction should be preserved") ||
        !check(convertedIncoming.sender() == QStringLiteral("peer@example.org"),
            "incoming sender should be preserved") ||
        !check(convertedIncoming.recipient() == QStringLiteral("me@example.org"),
            "incoming recipient should be preserved") ||
        !check(convertedIncoming.body() == QStringLiteral("hello from peer"),
            "incoming body should be preserved") ||
        !check(convertedIncoming.timestamp() == incomingTime,
            "incoming timestamp should be preserved") ||
        !check(convertedIncoming.conversationId() == conversationId,
            "conversation identity should be preserved") ||
        !check(convertedIncoming.protocol() == QStringLiteral("xmpp"),
            "protocol identifier should be preserved") ||
        !check(convertedIncoming.metadata().value(QStringLiteral("thread_id")).toString() ==
                QStringLiteral("thread-7"), "thread metadata should be preserved") ||
        !check(!convertedIncoming.messageId().isEmpty(),
            "converted message should have a stable non-empty history ID"))
        return 1;

    const BasicMessage &convertedOutgoing = converted.at(1);
    if (!check(convertedOutgoing.direction() == BasicMessage::Outgoing,
            "archive outgoing direction should be preserved") ||
        !check(convertedOutgoing.body() == QStringLiteral("hello back"),
            "outgoing body should be preserved"))
        return 1;

    const QList<BasicMessage> repeated = XmppArchiveHistory::convert(
        collection, conversationId, QStringLiteral("xmpp"));
    if (!check(repeated.at(0).messageId() == converted.at(0).messageId() &&
            repeated.at(1).messageId() == converted.at(1).messageId(),
            "history IDs should be deterministic across conversions"))
        return 1;

    const QList<BasicMessage> otherConversation = XmppArchiveHistory::convert(
        collection, QStringLiteral("other@example.org"), QStringLiteral("xmpp"));
    if (!check(otherConversation.at(0).conversationId() != converted.at(0).conversationId() &&
            otherConversation.at(0).messageId() != converted.at(0).messageId(),
            "separate conversations should remain separately identified"))
        return 1;

    return 0;
}
