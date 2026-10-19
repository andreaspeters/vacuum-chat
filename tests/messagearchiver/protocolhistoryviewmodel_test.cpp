#include "protocolhistoryviewmodel.h"

#include <QCoreApplication>
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

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const ConversationId room = QStringLiteral("!room:example.org");
    const QDate firstDay(2025, 3, 4);
    const QDate secondDay(2025, 3, 5);
    const auto makeMessage = [&room](const QString &id, const QString &body,
        const QDate &day, int hour, const QString &conversation = QString()) {
        return BasicMessage(id, conversation.isEmpty() ? room : conversation,
            QStringLiteral("@alice:example.org"), QStringLiteral("@self:example.org"), body,
            QDateTime(day, QTime(hour, 0), Qt::UTC), QStringLiteral("matrix"),
            BasicMessage::Incoming);
    };

    const QList<BasicMessage> input {
        makeMessage(QStringLiteral("later"), QStringLiteral("Second message"), firstDay, 11),
        makeMessage(QStringLiteral("other-room"), QStringLiteral("Do not include"), firstDay, 9,
            QStringLiteral("!other:example.org")),
        makeMessage(QStringLiteral("next-day"), QStringLiteral("Next day"), secondDay, 8),
        makeMessage(QStringLiteral("earlier"), QStringLiteral("First message"), firstDay, 9)
    };
    const QList<ProtocolHistoryDayGroup> all =
        ProtocolHistoryViewModel::groupByDay(input, room, QString());
    if (!check(all.size() == 2, "history should be grouped by date and isolated by conversation") ||
        !check(all.at(0).date == firstDay && all.at(0).messages.size() == 2,
            "first date group should contain only its two room messages") ||
        !check(all.at(0).messages.at(0).messageId() == QStringLiteral("earlier") &&
            all.at(0).messages.at(1).messageId() == QStringLiteral("later"),
            "messages within a date group should be chronological") ||
        !check(all.at(1).date == secondDay && all.at(1).messages.size() == 1,
            "second date group should contain its message"))
        return 1;

    const QList<ProtocolHistoryDayGroup> filtered =
        ProtocolHistoryViewModel::groupByDay(input, room, QStringLiteral("SECOND"));
    if (!check(filtered.size() == 1 && filtered.first().messages.size() == 1 &&
        filtered.first().messages.first().messageId() == QStringLiteral("later"),
        "search should match message text case-insensitively"))
        return 1;
    return 0;
}
