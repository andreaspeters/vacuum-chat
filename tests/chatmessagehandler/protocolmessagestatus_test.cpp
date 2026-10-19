#include <iostream>
#include <QString>

#include "protocolmessagestatus.h"

namespace
{
bool check(bool condition, const char *description)
{
	if (!condition)
		std::cerr << description << " failed\n";
	return condition;
}
}

int main()
{
	bool passed = true;
	const QString sentWithReader = ProtocolMessageStatus::buildReadReceiptFooter(
		true, {QStringLiteral("Alice")}, {});
	passed &= check(sentWithReader.contains(QStringLiteral("&#10003;")),
		"successful send renders a check mark");
	passed &= check(sentWithReader.contains(QStringLiteral("10px")) &&
		sentWithReader.contains(QStringLiteral("border-radius:50%")),
		"reader fallback has a 10px circular shape");
	passed &= check(sentWithReader.contains(QStringLiteral(">A</span>")),
		"missing avatar renders the reader initial");

	const QString notSent = ProtocolMessageStatus::buildReadReceiptFooter(
		false, {QStringLiteral("Bob")}, {});
	passed &= check(!notSent.contains(QStringLiteral("&#10003;")),
		"unsent message has no success check mark");

	const QString multipleReaders = ProtocolMessageStatus::buildReadReceiptFooter(
		true, {QStringLiteral("Alice"), QStringLiteral("Bob")},
		{QStringLiteral("vacuum-avatar:/alice"), QString()});
	passed &= check(multipleReaders.contains(QStringLiteral("src=\"vacuum-avatar:/alice\"")) &&
		multipleReaders.contains(QStringLiteral("width=\"10\"")) &&
		multipleReaders.contains(QStringLiteral("height=\"10\"")),
		"local avatar resource is rendered as a 10px image");
	passed &= check(multipleReaders.contains(QStringLiteral("Alice&#10;Bob")),
		"first reader tooltip lists all reader names on separate lines");

	const QString escapedName = ProtocolMessageStatus::buildReadReceiptFooter(
		false, {QStringLiteral("<b>Alice</b>")}, {});
	passed &= check(escapedName.contains(QStringLiteral("&lt;b&gt;Alice&lt;/b&gt;")) &&
		!escapedName.contains(QStringLiteral("<b>Alice</b>")),
		"reader name is HTML-escaped");

	const QString externalAvatar = ProtocolMessageStatus::buildReadReceiptFooter(
		false, {QStringLiteral("Carol")}, {QStringLiteral("https://example.invalid/avatar.png")});
	passed &= check(!externalAvatar.contains(QStringLiteral("https://")),
		"external avatar URLs are not embedded");

	ProtocolMessageStatus::StateStore stateStore;
	stateStore.updateDelivery(QStringLiteral("account-a"), QStringLiteral("room-a"),
		QStringLiteral("txn-a"), QStringLiteral("sent"), QStringLiteral("$event-a"));
	stateStore.updateReadReceipt(QStringLiteral("account-a"), QStringLiteral("room-a"),
		QStringLiteral("$event-a"), QStringLiteral("@alice"), QStringLiteral("Alice"));
	stateStore.updateReadReceipt(QStringLiteral("account-a"), QStringLiteral("room-a"),
		QStringLiteral("$event-a"), QStringLiteral("@bob"), QStringLiteral("Bob"));
	stateStore.updateReadReceipt(QStringLiteral("account-a"), QStringLiteral("room-a"),
		QStringLiteral("$event-a"), QStringLiteral("@alice"), QStringLiteral("Alice A."));
	const ProtocolMessageStatus::MessageState echoedState = stateStore.stateFor(
		QStringLiteral("account-a"), QStringLiteral("room-a"),
		QStringLiteral("$event-a"), QStringLiteral("txn-a"));
	passed &= check(echoedState.sentSuccessfully && echoedState.readers.size() == 2,
		"server event and transaction IDs resolve to one sent message with unique readers");
	passed &= check(echoedState.readers.first().userId == QStringLiteral("@alice") &&
		echoedState.readers.first().displayName == QStringLiteral("Alice A."),
		"duplicate reader receipt updates the reader name without duplicating the avatar");
	const ProtocolMessageStatus::MessageState localEchoState = stateStore.stateFor(
		QStringLiteral("account-a"), QStringLiteral("room-a"), QStringLiteral("txn-a"));
	passed &= check(localEchoState.sentSuccessfully && localEchoState.readers.size() == 2,
		"receipt received before the sync echo is visible on the local transaction message");
	const ProtocolMessageStatus::MessageState isolatedState = stateStore.stateFor(
		QStringLiteral("account-b"), QStringLiteral("room-a"), QStringLiteral("$event-a"));
	passed &= check(!isolatedState.sentSuccessfully && isolatedState.readers.isEmpty(),
		"message status is isolated by account and conversation");

	stateStore.updateDelivery(QStringLiteral("account-a"), QStringLiteral("room-a"),
		QStringLiteral("txn-failed"), QStringLiteral("failed"), QString());
	const ProtocolMessageStatus::MessageState failedState = stateStore.stateFor(
		QStringLiteral("account-a"), QStringLiteral("room-a"), QStringLiteral("txn-failed"));
	passed &= check(!failedState.sentSuccessfully,
		"failed delivery never reports successful send");

	return passed ? 0 : 1;
}