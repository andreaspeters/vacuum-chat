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
