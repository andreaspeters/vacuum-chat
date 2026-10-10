#include "ax25chatmessagerouting.h"

#include <QDateTime>
#include <QTimeZone>
#include <iostream>

namespace
{
bool check(bool condition, const char *message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}
}

int main()
{
    bool passed = true;
    passed &= check(Ax25ChatMessageRouting::isRemoteCallsign(
                        QStringLiteral("ABC123"), QStringLiteral("N0CALL")),
                    "messages from another station are eligible for incoming routing");
    passed &= check(!Ax25ChatMessageRouting::isRemoteCallsign(
                        QStringLiteral("N0CALL"), QStringLiteral("N0CALL")),
                    "a locally echoed frame is not an incoming remote contact");
    passed &= check(!Ax25ChatMessageRouting::isRemoteCallsign(
                        QStringLiteral("n0call"), QStringLiteral("N0CALL")),
                    "local echo detection is case-insensitive");

    const QDateTime timestamp = QDateTime::fromMSecsSinceEpoch(1234, QTimeZone::utc());
    const BasicMessage localEcho = Ax25ChatMessageRouting::createOutgoingMessage(
        QStringLiteral("ABC123"), QStringLiteral("N0CALL"), QStringLiteral("hello"),
        0x1234u, timestamp);
    passed &= check(localEcho.conversationId() == QStringLiteral("ABC123") &&
                        localEcho.sender() == QStringLiteral("N0CALL") &&
                        localEcho.recipient() == QStringLiteral("ABC123") &&
                        localEcho.body() == QStringLiteral("hello") &&
                        localEcho.direction() == BasicMessage::Outgoing &&
                        localEcho.messageId().startsWith(QStringLiteral("ax25:tx:ABC123:")),
                    "outgoing local echo belongs to the selected remote contact conversation");
    return passed ? 0 : 1;
}
