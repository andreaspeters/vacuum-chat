#ifndef PROTOCOLMESSAGESTATUS_H
#define PROTOCOLMESSAGESTATUS_H

#include <QString>
#include <QList>
#include <QMap>

namespace ProtocolMessageStatus
{
	QString buildReadReceiptFooter(bool AIsSentSuccess, const QList<QString> &AReaderNames, const QList<QString> &AAvatarUrls);

	struct Reader
	{
		QString userId;
		QString displayName;
	};

	struct MessageState
	{
		bool sentSuccessfully = false;
		QList<Reader> readers;
	};

	class StateStore
	{
	public:
		void updateDelivery(const QString &AStreamId, const QString &AConversationId,
			const QString &ATransactionId, const QString &AStatus,
			const QString &AServerEventId);
		void updateReadReceipt(const QString &AStreamId, const QString &AConversationId,
			const QString &AEventId, const QString &AReaderId, const QString &ADisplayName);
		MessageState stateFor(const QString &AStreamId, const QString &AConversationId,
			const QString &AMessageId, const QString &ATransactionId = QString()) const;

	private:
		QMap<QString, MessageState> FStates;
		QMap<QString, QString> FAliases;
	};
}

#endif // PROTOCOLMESSAGESTATUS_H