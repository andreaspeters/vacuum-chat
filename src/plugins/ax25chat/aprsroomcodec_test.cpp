#include "aprsroomcodec.h"

#include <iostream>

namespace
{
bool check(bool condition, const char *message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

bool testJoinAndLeaveCommands()
{
    QByteArray wire;
    QString error;
    if (!check(AprsRoom::Codec::encodeJoinCommand(QStringLiteral("SOTA"), &wire, &error),
               "valid room join command must encode"))
        return false;
    if (!check(wire == QByteArrayLiteral("J SOTA"), "join command must use APRS ANSRVR syntax"))
        return false;
    if (!check(AprsRoom::Codec::encodeLeaveCommand(QStringLiteral("SOTA"), &wire, &error),
               "valid room leave command must encode"))
        return false;
    return check(wire == QByteArrayLiteral("U SOTA"), "leave command must use APRS ANSRVR syntax");
}

bool testGroupMessageRoundTrip()
{
    AprsRoom::RoomMessage input;
    input.roomName = QStringLiteral("SOTA");
    input.body = QStringLiteral("Summit alert");
    QByteArray wire;
    QString error;
    if (!AprsRoom::Codec::encode(input, &wire, &error)) {
        std::cerr << error.toStdString() << '\n';
        return false;
    }
    if (!check(wire == QByteArrayLiteral("CQ SOTA Summit alert"),
               "group message must use CQ <group> <text> syntax"))
        return false;

    AprsRoom::RoomMessage output;
    if (!AprsRoom::Codec::decode(wire, &output, &error)) {
        std::cerr << error.toStdString() << '\n';
        return false;
    }
    return check(output.roomName == input.roomName && output.body == input.body,
                 "group name and user text must round-trip");
}

bool testGroupNameIsCaseInsensitive()
{
    AprsRoom::RoomMessage input;
    input.roomName = QStringLiteral("sota");
    input.body = QStringLiteral("hello");
    QByteArray wire;
    QString error;
    if (!AprsRoom::Codec::encode(input, &wire, &error) ||
        !check(wire == QByteArrayLiteral("CQ SOTA hello"),
               "outgoing group names must be canonical uppercase"))
        return false;
    AprsRoom::RoomMessage output;
    return check(AprsRoom::Codec::decode(QByteArrayLiteral("cq sota hello"), &output, &error) &&
                 output.roomName == QStringLiteral("SOTA") && output.body == QStringLiteral("hello"),
                 "incoming group names must be matched case-insensitively");
}

bool testMalformedGroupPayloads()
{
    AprsRoom::RoomMessage output;
    QString error;
    return check(!AprsRoom::Codec::decode(QByteArrayLiteral("CQ SOTA"), &output, &error),
                 "missing separator and text must be rejected") &&
           check(!AprsRoom::Codec::decode(QByteArrayLiteral("CQ  hello"), &output, &error),
                 "empty group name must be rejected") &&
           check(!AprsRoom::Codec::decode(QByteArrayLiteral("CQ SOTA  "), &output, &error),
                 "empty group message must be rejected") &&
           check(!AprsRoom::Codec::decode(QByteArrayLiteral("CQ SOTA hello\n"), &output, &error),
                 "control characters must be rejected");
}

bool testRoomNameBoundaries()
{
    QByteArray wire;
    QString error;
    if (!check(AprsRoom::Codec::encodeJoinCommand(QString(46, QLatin1Char('A')), &wire, &error),
               "46-character ANSRVR room name must be accepted"))
        return false;
    if (!check(!AprsRoom::Codec::encodeJoinCommand(QString(47, QLatin1Char('A')), &wire, &error),
               "47-character room name must be rejected"))
        return false;
    return check(!AprsRoom::Codec::encodeJoinCommand(QStringLiteral("BAD NAME"), &wire, &error),
                 "room names containing spaces must be rejected");
}

bool testFullAprsTextLengthIncludesGroupPrefix()
{
    // "CQ " + 46-character group name + space consumes 50 of 67 APRS text characters.
    AprsRoom::RoomMessage message;
    message.roomName = QString(46, QLatin1Char('A'));
    message.body = QString(17, QLatin1Char('x'));
    QByteArray wire;
    QString error;
    if (!check(AprsRoom::Codec::encode(message, &wire, &error) && wire.size() == 67,
               "group payload at the 67-character APRS limit must be accepted"))
        return false;
    message.body.append(QLatin1Char('x'));
    return check(!AprsRoom::Codec::encode(message, &wire, &error),
                 "CQ prefix and room name must count toward the 67-character APRS text limit");
}
}

int main()
{
    const bool ok = testJoinAndLeaveCommands() && testGroupMessageRoundTrip() &&
        testGroupNameIsCaseInsensitive() && testMalformedGroupPayloads() &&
        testRoomNameBoundaries() && testFullAprsTextLengthIncludesGroupPrefix();
    return ok ? 0 : 1;
}
