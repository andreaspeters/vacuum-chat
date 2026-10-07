#include "protocolmessagehistory.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimeZone>
#include <QTimer>
#include <QSet>
#include <iostream>

namespace {
bool check(bool condition, const char *description)
{
	if (!condition)
		std::cerr << "FAIL: " << description << '\n';
	return condition;
}
}

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
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
	const QString emptyHistoryKey = QStringLiteral("account-stream\n!empty:example.org");
	QSet<QString> loadingConversations{emptyHistoryKey};
	QSet<QString> loadedConversations;
	ProtocolMessageHistory::finishHistoryLoad(loadingConversations, loadedConversations,
		emptyHistoryKey, false);
	passed &= check(!loadingConversations.contains(emptyHistoryKey) &&
		!loadedConversations.contains(emptyHistoryKey),
		"empty history clears loading without permanently marking the conversation loaded");

	const QString populatedHistoryKey = QStringLiteral("account-stream\n!populated:example.org");
	loadingConversations.insert(populatedHistoryKey);
	ProtocolMessageHistory::finishHistoryLoad(loadingConversations, loadedConversations,
		populatedHistoryKey, true);
	passed &= check(!loadingConversations.contains(populatedHistoryKey) &&
		loadedConversations.contains(populatedHistoryKey),
		"non-empty history clears loading and records completion");

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
	QList<BasicMessage> visibleHistory{serverEcho, laterMessage};
	BasicMessage staleOverlap = serverEcho;
	staleOverlap.setBody(QStringLiteral("stale overlap"));
	const QList<BasicMessage> olderPage{olderMessage, staleOverlap};
	const QList<BasicMessage> mergedOlderPage =
		ProtocolMessageHistory::mergeOlderPage(visibleHistory, olderPage);
	passed &= check(mergedOlderPage.size() == 3 &&
		mergedOlderPage.at(0).messageId() == olderMessage.messageId() &&
		mergedOlderPage.at(1).messageId() == serverEcho.messageId() &&
		mergedOlderPage.at(2).messageId() == laterMessage.messageId(),
		"older page merges before visible messages in chronological order without duplicates");
	passed &= check(mergedOlderPage.at(1).body() == serverEcho.body(),
		"an overlapping page cannot overwrite the already-visible version of a message");

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

	BasicMessage imagePlaceholder(QStringLiteral("$old-image"), QStringLiteral("!room:test"),
		QStringLiteral("@other:test"), QString(), QStringLiteral("photo.png"),
		QDateTime::fromMSecsSinceEpoch(3000, QTimeZone::utc()),
		QStringLiteral("matrix"), BasicMessage::Incoming);
	imagePlaceholder.setMetadata({{QStringLiteral("msgtype"), QStringLiteral("m.image")},
		{QStringLiteral("historical"), true},
		{QStringLiteral("url"), QStringLiteral("mxc://media.example.org/photo")}});
	BasicMessage hydratedImage = imagePlaceholder;
	hydratedImage.setMetadata({{QStringLiteral("msgtype"), QStringLiteral("m.image")},
		{QStringLiteral("historical"), true}, {QStringLiteral("file_path"), QStringLiteral("/cache/photo.bin")},
		{QStringLiteral("url"), QStringLiteral("mxc://media.example.org/photo")},
		{QStringLiteral("decoded_image"), QStringLiteral("decoded-payload")}});
	passed &= check(ProtocolMessageHistory::isMediaHydrationUpdate(imagePlaceholder, hydratedImage),
		"same-event image payload is recognized as media hydration");
	passed &= check(!ProtocolMessageHistory::isMediaHydrationUpdate(hydratedImage, hydratedImage),
		"repeated image hydration is idempotent");
	BasicMessage differentImage = hydratedImage;
	differentImage.setMessageId(QStringLiteral("$different-image"));
	passed &= check(!ProtocolMessageHistory::isMediaHydrationUpdate(imagePlaceholder, differentImage),
		"a different Matrix event is not treated as media hydration");
	QList<BasicMessage> partiallyLoadedHistory{imagePlaceholder, laterMessage};
	const int replacedImageIndex = ProtocolMessageHistory::replaceExistingMessage(
		partiallyLoadedHistory, hydratedImage);
	passed &= check(replacedImageIndex == 0 && partiallyLoadedHistory.size() == 2 &&
		partiallyLoadedHistory.first().messageId() == QStringLiteral("$old-image") &&
		partiallyLoadedHistory.first().metadata().value(QStringLiteral("decoded_image")).toString() ==
			QStringLiteral("decoded-payload") &&
		partiallyLoadedHistory.last().messageId() == laterMessage.messageId(),
		"hydrating a historical image replaces its existing event without appending it");
	passed &= check(ProtocolMessageHistory::replaceExistingMessage(partiallyLoadedHistory,
		serverEcho) == -1 && partiallyLoadedHistory.size() == 2,
		"a different event ID is not mistaken for a media update");

	ProtocolMessageHistory::ConversationRebuildScheduler rebuildScheduler(25);
	QEventLoop rebuildLoop;
	int staleWindowRebuilds = 0;
	int currentWindowRebuilds = 0;
	int otherConversationRebuilds = 0;
	auto finishRebuildBatch = [&rebuildLoop, &currentWindowRebuilds, &otherConversationRebuilds]() {
		if (currentWindowRebuilds + otherConversationRebuilds == 2)
			rebuildLoop.quit();
	};
	rebuildScheduler.request(QStringLiteral("matrix\n!room:test"), [&staleWindowRebuilds]() {
		++staleWindowRebuilds;
	});
	rebuildScheduler.request(QStringLiteral("matrix\n!room:test"), [&currentWindowRebuilds, &finishRebuildBatch]() {
		++currentWindowRebuilds;
		finishRebuildBatch();
	});
	rebuildScheduler.request(QStringLiteral("matrix\n!other:test"), [&otherConversationRebuilds, &finishRebuildBatch]() {
		++otherConversationRebuilds;
		finishRebuildBatch();
	});
	passed &= check(staleWindowRebuilds == 0 && currentWindowRebuilds == 0 &&
		otherConversationRebuilds == 0,
		"timeline rebuilds are deferred until the bounded batch timer fires");
	QTimer::singleShot(1000, &rebuildLoop, &QEventLoop::quit);
	rebuildLoop.exec();
	passed &= check(staleWindowRebuilds == 0 && currentWindowRebuilds == 1,
		"a burst schedules only the latest rebuild callback for that conversation");
	passed &= check(otherConversationRebuilds == 1,
		"rebuilds for distinct conversations are both preserved");

	ProtocolMessageHistory::OlderHistoryPageState pageState;
	passed &= check(pageState.beginRequest(), "first older-history request starts");
	passed &= check(!pageState.beginRequest(), "a duplicate in-flight page request is rejected");
	pageState.finishRequest(false, false, false);
	passed &= check(!pageState.isInFlight() && !pageState.isExhausted() && pageState.beginRequest(),
		"a failed page request clears in-flight state and remains retryable");
	pageState.finishRequest(true, true, true);
	passed &= check(!pageState.isExhausted() && pageState.beginRequest(),
		"hasMore allows another older-history page");
	pageState.finishRequest(true, false, true);
	passed &= check(pageState.isExhausted() && !pageState.beginRequest(),
		"a successful exhausted page stops future requests");
	ProtocolMessageHistory::OlderHistoryPageState noProgressState;
	passed &= check(noProgressState.beginRequest(), "no-progress test request starts");
	noProgressState.finishRequest(true, true, false);
	passed &= check(noProgressState.isExhausted(),
		"a successful page without new messages cannot trigger an infinite retry loop");

	passed &= check(ProtocolMessageHistory::preservedScrollPosition(40, 100, 160) == 100,
		"prepended content preserves the visible scroll offset");
	passed &= check(ProtocolMessageHistory::preservedScrollPosition(0, 100, 160) == 60,
		"a top-of-history view remains anchored after prepending");
	passed &= check(ProtocolMessageHistory::preservedScrollPosition(20, 100, 50) == 0,
		"scroll restoration clamps when the new range is smaller");

	return passed ? 0 : 1;
}
