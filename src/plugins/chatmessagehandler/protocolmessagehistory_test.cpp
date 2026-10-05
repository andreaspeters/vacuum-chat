#include "protocolmessagehistory.h"

#include <QTimeZone>
#include <iostream>

namespace {
bool check(bool condition, const char *description)
{
	if (!condition)
		std::cerr << "FAIL: " << description << '\n';
	return condition;
}
}

int main()
{
	const QString transactionId = QStringLiteral("txn-reply-1");
	BasicMessage localEcho(QStringLiteral("txn-reply-1"), QStringLiteral("!room:test"),
		QStringLiteral("@me:test"), QString(), QStringLiteral("answer"),
		QDateTime::fromMSecsSinceEpoch(2000, QTimeZone::utc()),
		QStringLiteral("matrix"), BasicMessage::Outgoing);
	localEcho.setMetadata({{QStringLiteral("txn_id"), transactionId}});

	BasicMessage serverEcho(QStringLiteral("$server-event"), QStringLiteral("!room:test"),
		QStringLiteral("@me:test"), QString(), QStringLiteral("answer"),
		QDateTime::fromMSecsSinceEpoch(1000, QTimeZone::utc()),
		QStringLiteral("matrix"), BasicMessage::Incoming);
	serverEcho.setMetadata({{QStringLiteral("replaces_txn_id"), transactionId}});
	BasicMessage pendingEncryptedEcho = serverEcho;
	pendingEncryptedEcho.setBody(QString());
	QVariantMap encryptedMetadata = pendingEncryptedEcho.metadata();
	encryptedMetadata.insert(QStringLiteral("outer_event_type"), QStringLiteral("m.room.encrypted"));
	encryptedMetadata.insert(QStringLiteral("decryption_status"), QStringLiteral("pending"));
	pendingEncryptedEcho.setMetadata(encryptedMetadata);

	bool passed = true;
	QList<BasicMessage> encryptedHistory;
	encryptedHistory.append(localEcho);
	const auto pendingMerge = ProtocolMessageHistory::mergeTransactionEcho(encryptedHistory,
		pendingEncryptedEcho);
	passed &= check(pendingMerge == ProtocolMessageHistory::TransactionEchoMergeResult::RetainedLocalEcho,
		"encrypted server echo is associated with its local echo");
	passed &= check(encryptedHistory.size() == 1 &&
		encryptedHistory.first().messageId() == transactionId &&
		encryptedHistory.first().body() == QStringLiteral("answer"),
		"pending decryption retains the visible plaintext local echo");
	passed &= check(encryptedHistory.first().timestamp().toMSecsSinceEpoch() == 1000,
		"pending encrypted echo advances the local echo to the server timestamp");
	QList<BasicMessage> reverseEncryptedHistory;
	reverseEncryptedHistory.append(pendingEncryptedEcho);
	const auto restoredLocalEcho = ProtocolMessageHistory::mergeTransactionEcho(
		reverseEncryptedHistory, localEcho);
	passed &= check(restoredLocalEcho ==
		ProtocolMessageHistory::TransactionEchoMergeResult::RestoredLocalEcho &&
		reverseEncryptedHistory.size() == 1 &&
		reverseEncryptedHistory.first().messageId() == transactionId &&
		reverseEncryptedHistory.first().body() == QStringLiteral("answer") &&
		reverseEncryptedHistory.first().timestamp().toMSecsSinceEpoch() == 1000,
		"a late local echo replaces a pending encrypted server event");
	BasicMessage decryptedServerEcho = serverEcho;
	QVariantMap decryptedMetadata = decryptedServerEcho.metadata();
	decryptedMetadata.insert(QStringLiteral("outer_event_type"), QStringLiteral("m.room.encrypted"));
	decryptedMetadata.insert(QStringLiteral("decryption_status"), QStringLiteral("decrypted"));
	decryptedServerEcho.setMetadata(decryptedMetadata);
	const auto completedMerge = ProtocolMessageHistory::mergeTransactionEcho(
		reverseEncryptedHistory, decryptedServerEcho);
	passed &= check(completedMerge ==
		ProtocolMessageHistory::TransactionEchoMergeResult::ReplacedLocalEcho &&
		reverseEncryptedHistory.size() == 1 &&
		reverseEncryptedHistory.first().messageId() == serverEcho.messageId(),
		"decrypted server echo replaces the retained local echo");

	QList<BasicMessage> history;
	history.append(localEcho);
	history.append(serverEcho);
	const auto replaced = ProtocolMessageHistory::mergeTransactionEcho(history, serverEcho);
	passed &= check(replaced == ProtocolMessageHistory::TransactionEchoMergeResult::ReplacedLocalEcho,
		"server echo replaces the local echo by transaction ID");
	passed &= check(history.size() == 1 && history.first().messageId() == QStringLiteral("$server-event"),
		"replacement leaves one canonical event in conversation history");

	QList<BasicMessage> reverseOrder;
	reverseOrder.append(serverEcho);
	const auto ignored = ProtocolMessageHistory::mergeTransactionEcho(reverseOrder, localEcho);
	passed &= check(ignored == ProtocolMessageHistory::TransactionEchoMergeResult::IgnoredLocalEcho,
		"late local echo is ignored when its server event is already cached");
	passed &= check(reverseOrder.size() == 1 && reverseOrder.first().messageId() == QStringLiteral("$server-event"),
		"reverse arrival order does not restore a duplicate local echo");

	QList<BasicMessage> unrelated;
	const auto unmatched = ProtocolMessageHistory::mergeTransactionEcho(unrelated, serverEcho);
	passed &= check(unmatched == ProtocolMessageHistory::TransactionEchoMergeResult::NoMatch && unrelated.isEmpty(),
		"unmatched transaction does not mutate the history");

	BasicMessage olderMessage(QStringLiteral("$older-event"), QStringLiteral("!room:test"),
		QStringLiteral("@other:test"), QString(), QStringLiteral("earlier"),
		QDateTime::fromMSecsSinceEpoch(500, QTimeZone::utc()),
		QStringLiteral("matrix"), BasicMessage::Incoming);
	QList<BasicMessage> samePositionHistory;
	samePositionHistory.append(olderMessage);
	samePositionHistory.append(localEcho);
	const int originalIndex = ProtocolMessageHistory::findTransactionEchoIndex(
		samePositionHistory, serverEcho);
	const auto samePositionMerge = ProtocolMessageHistory::mergeTransactionEcho(
		samePositionHistory, serverEcho);
	const int currentIndex = ProtocolMessageHistory::findMessageIndex(
		samePositionHistory, serverEcho.messageId());
	passed &= check(samePositionMerge == ProtocolMessageHistory::TransactionEchoMergeResult::ReplacedLocalEcho &&
		originalIndex == 1 && currentIndex == originalIndex,
		"in-order server echo stays at the rendered local echo position");
	passed &= check(!ProtocolMessageHistory::requiresTimelineRebuild(
		originalIndex, currentIndex, samePositionHistory.size(), true),
		"unchanged rendered position does not require a full timeline rebuild");
	passed &= check(!ProtocolMessageHistory::requiresChronologicalSort(samePositionHistory, currentIndex),
		"in-order echo replacement does not sort the full conversation");
	BasicMessage laterMessage(QStringLiteral("$later-event"), QStringLiteral("!room:test"),
		QStringLiteral("@other:test"), QString(), QStringLiteral("later"),
		QDateTime::fromMSecsSinceEpoch(1500, QTimeZone::utc()),
		QStringLiteral("matrix"), BasicMessage::Incoming);
	QList<BasicMessage> outOfOrderHistory;
	outOfOrderHistory.append(laterMessage);
	outOfOrderHistory.append(localEcho);
	const int outOfOrderIndex = ProtocolMessageHistory::findTransactionEchoIndex(
		outOfOrderHistory, serverEcho);
	ProtocolMessageHistory::mergeTransactionEcho(outOfOrderHistory, serverEcho);
	const int replacementIndex = ProtocolMessageHistory::findMessageIndex(
		outOfOrderHistory, serverEcho.messageId());
	passed &= check(outOfOrderIndex == 1 &&
		ProtocolMessageHistory::requiresChronologicalSort(outOfOrderHistory, replacementIndex),
		"out-of-order echo replacement requires sorting before rebuild");
	passed &= check(ProtocolMessageHistory::requiresTimelineRebuild(1, 0, 2, true),
		"a transaction echo that moves earlier still requires timeline reordering");
	passed &= check(ProtocolMessageHistory::requiresTimelineRebuild(-1, 1, 3, false) &&
		!ProtocolMessageHistory::requiresTimelineRebuild(-1, 2, 3, false),
		"an unrendered item rebuilds only when it belongs before the visible tail");
	return passed ? 0 : 1;
}
