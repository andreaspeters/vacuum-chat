#ifndef PROTOCOLMESSAGEHISTORY_H
#define PROTOCOLMESSAGEHISTORY_H

#include <interfaces/imessage.h>

#include <QList>
#include <QString>

#include <algorithm>

namespace ProtocolMessageHistory
{
enum class TransactionEchoMergeResult
{
	NoMatch,
	ReplacedLocalEcho,
	RetainedLocalEcho,
	RestoredLocalEcho,
	IgnoredLocalEcho
};

inline bool isPendingEncryptedEvent(const BasicMessage &AMessage)
{
	const QVariantMap metadata = AMessage.metadata();
	return metadata.value(QStringLiteral("outer_event_type")).toString() ==
		QStringLiteral("m.room.encrypted") &&
		metadata.value(QStringLiteral("decryption_status")).toString() != QStringLiteral("decrypted") &&
		!metadata.value(QStringLiteral("redacted")).toBool();
}

inline int findMessageIndex(const QList<BasicMessage> &AHistory, const QString &AMessageId)
{
	if (AMessageId.isEmpty())
		return -1;
	const auto it = std::find_if(AHistory.cbegin(), AHistory.cend(),
		[&AMessageId](const BasicMessage &message) { return message.messageId() == AMessageId; });
	return it == AHistory.cend() ? -1 : static_cast<int>(std::distance(AHistory.cbegin(), it));
}

inline int findTransactionEchoIndex(const QList<BasicMessage> &AHistory, const BasicMessage &AMessage)
{
	if (AMessage.protocol() != QStringLiteral("matrix"))
		return -1;

	const QString replacesTxnId = AMessage.metadata().value(QStringLiteral("replaces_txn_id")).toString();
	if (!replacesTxnId.isEmpty()) {
		const auto it = std::find_if(AHistory.cbegin(), AHistory.cend(),
			[&replacesTxnId](const BasicMessage &message) {
				return message.messageId() == replacesTxnId ||
					message.metadata().value(QStringLiteral("txn_id")).toString() == replacesTxnId;
			});
		return it == AHistory.cend() ? -1 : static_cast<int>(std::distance(AHistory.cbegin(), it));
	}

	const QString transactionId = AMessage.metadata().value(QStringLiteral("txn_id")).toString();
	if (transactionId.isEmpty())
		return -1;
	const auto it = std::find_if(AHistory.cbegin(), AHistory.cend(),
		[&transactionId](const BasicMessage &message) {
			return message.metadata().value(QStringLiteral("replaces_txn_id")).toString() == transactionId;
		});
	return it == AHistory.cend() ? -1 : static_cast<int>(std::distance(AHistory.cbegin(), it));
}

inline bool requiresChronologicalSort(const QList<BasicMessage> &AHistory, int AIndex)
{
	if (AIndex < 0 || AIndex >= AHistory.size())
		return true;
	const auto precedes = [](const BasicMessage &left, const BasicMessage &right) {
		const QDateTime leftTime = left.timestamp();
		const QDateTime rightTime = right.timestamp();
		if (leftTime.isValid() != rightTime.isValid())
			return leftTime.isValid();
		if (!leftTime.isValid())
			return false;
		return leftTime.toMSecsSinceEpoch() < rightTime.toMSecsSinceEpoch();
	};
	if (AIndex > 0 && precedes(AHistory.at(AIndex), AHistory.at(AIndex - 1)))
		return true;
	return AIndex + 1 < AHistory.size() &&
		precedes(AHistory.at(AIndex + 1), AHistory.at(AIndex));
}

inline bool requiresTimelineRebuild(int APreviousIndex, int ACurrentIndex,
	int AHistorySize, bool APreviouslyRendered)
{
	if (ACurrentIndex < 0)
		return true;
	if (APreviousIndex >= 0 && APreviousIndex != ACurrentIndex)
		return true;
	return !APreviouslyRendered && ACurrentIndex != AHistorySize - 1;
}

inline TransactionEchoMergeResult mergeTransactionEcho(QList<BasicMessage> &AHistory,
	const BasicMessage &AMessage)
{
	if (AMessage.protocol() != QStringLiteral("matrix"))
		return TransactionEchoMergeResult::NoMatch;

	const QString replacesTxnId = AMessage.metadata().value(QStringLiteral("replaces_txn_id")).toString();
	if (!replacesTxnId.isEmpty() && !AMessage.messageId().isEmpty()) {
		auto localEcho = std::find_if(AHistory.begin(), AHistory.end(),
			[&replacesTxnId](const BasicMessage &message) {
				return message.messageId() == replacesTxnId ||
					message.metadata().value(QStringLiteral("txn_id")).toString() == replacesTxnId;
			});
		if (localEcho != AHistory.end()) {
			if (isPendingEncryptedEvent(AMessage)) {
				if (AMessage.timestamp().isValid())
					localEcho->setTimestamp(AMessage.timestamp());
				return TransactionEchoMergeResult::RetainedLocalEcho;
			}
			*localEcho = AMessage;
			bool keptCanonicalEvent = false;
			for (auto it = AHistory.begin(); it != AHistory.end();) {
				if (it->messageId() == AMessage.messageId()) {
					if (!keptCanonicalEvent) {
						*it = AMessage;
						keptCanonicalEvent = true;
						++it;
					} else {
						it = AHistory.erase(it);
					}
				} else {
					++it;
				}
			}
			return TransactionEchoMergeResult::ReplacedLocalEcho;
		}
	}

	const QString transactionId = AMessage.metadata().value(QStringLiteral("txn_id")).toString();
	if (!transactionId.isEmpty()) {
		auto serverEcho = std::find_if(AHistory.begin(), AHistory.end(),
			[&transactionId](const BasicMessage &message) {
				return message.metadata().value(QStringLiteral("replaces_txn_id")).toString() == transactionId;
			});
		if (serverEcho != AHistory.end()) {
			if (isPendingEncryptedEvent(*serverEcho)) {
				BasicMessage restoredLocalEcho = AMessage;
				if (serverEcho->timestamp().isValid())
					restoredLocalEcho.setTimestamp(serverEcho->timestamp());
				*serverEcho = restoredLocalEcho;
				return TransactionEchoMergeResult::RestoredLocalEcho;
			}
			return TransactionEchoMergeResult::IgnoredLocalEcho;
		}
	}

	return TransactionEchoMergeResult::NoMatch;
}
}

#endif // PROTOCOLMESSAGEHISTORY_H
