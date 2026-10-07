#ifndef PROTOCOLMESSAGEHISTORY_H
#define PROTOCOLMESSAGEHISTORY_H

#include <interfaces/imessage.h>

#include <QList>
#include <QHash>
#include <QSet>
#include <QString>
#include <QTimer>

#include <algorithm>
#include <functional>
#include <utility>

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

inline void finishHistoryLoad(QSet<QString> &ALoading, QSet<QString> &ALoaded,
	const QString &AConversationKey, bool AHasHistory)
{
	ALoading.remove(AConversationKey);
	if (AHasHistory)
		ALoaded.insert(AConversationKey);
}

class ConversationRebuildScheduler
{
public:
	explicit ConversationRebuildScheduler(int AIntervalMs = 25) : FIntervalMs(AIntervalMs)
	{
		FTimer.setSingleShot(true);
		QObject::connect(&FTimer, &QTimer::timeout, &FTimer, [this]() {
			QHash<QString, std::function<void()>> callbacks;
			callbacks.swap(FCallbacks);
			for (auto it = callbacks.cbegin(); it != callbacks.cend(); ++it)
				if (it.value())
					it.value()();
		});
	}

	void request(const QString &AConversationKey, std::function<void()> ACallback)
	{
		if (AConversationKey.isEmpty() || !ACallback)
			return;
		FCallbacks.insert(AConversationKey, std::move(ACallback));
		// Keep a fixed batch window so continuous traffic cannot starve a rebuild.
		if (!FTimer.isActive())
			FTimer.start(qMax(0, FIntervalMs));
	}

private:
	QTimer FTimer;
	QHash<QString, std::function<void()>> FCallbacks;
	int FIntervalMs;
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

inline int replaceExistingMessage(QList<BasicMessage> &AHistory, const BasicMessage &AMessage)
{
	const int index = findMessageIndex(AHistory, AMessage.messageId());
	if (index >= 0)
		AHistory[index] = AMessage;
	return index;
}

inline bool hasMediaPayload(const BasicMessage &AMessage)
{
	const QVariantMap metadata = AMessage.metadata();
	const QString messageType = metadata.value(QStringLiteral("msgtype")).toString();
	if (messageType == QStringLiteral("m.image")) {
		const QVariant decodedImage = metadata.value(QStringLiteral("decoded_image"));
		return (decodedImage.isValid() && !decodedImage.isNull()) ||
			!metadata.value(QStringLiteral("file_path")).toString().isEmpty();
	}
	return messageType == QStringLiteral("m.file") &&
		!metadata.value(QStringLiteral("file_path")).toString().isEmpty();
}

inline bool isMediaHydrationUpdate(const BasicMessage &AExisting, const BasicMessage &AUpdated)
{
	if (AExisting.protocol() != QStringLiteral("matrix") ||
		AUpdated.protocol() != QStringLiteral("matrix") ||
		AExisting.messageId().isEmpty() || AExisting.messageId() != AUpdated.messageId() ||
		AExisting.conversationId() != AUpdated.conversationId())
		return false;
	const QVariantMap oldMetadata = AExisting.metadata();
	const QVariantMap newMetadata = AUpdated.metadata();
	const QString oldType = oldMetadata.value(QStringLiteral("msgtype")).toString();
	const QString newType = newMetadata.value(QStringLiteral("msgtype")).toString();
	if (oldType != newType ||
		(oldType != QStringLiteral("m.image") && oldType != QStringLiteral("m.file")))
		return false;
	const QString oldUrl = oldMetadata.value(QStringLiteral("url")).toString();
	const QString newUrl = newMetadata.value(QStringLiteral("url")).toString();
	if (!oldUrl.isEmpty() && oldUrl != newUrl)
		return false;
	return !hasMediaPayload(AExisting) && hasMediaPayload(AUpdated);
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
