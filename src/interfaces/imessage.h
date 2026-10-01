#ifndef IMESSAGE_H
#define IMESSAGE_H

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QVariantMap>

// Protocol-neutral message value. The legacy Message/Stanza type remains the
// XMPP adapter boundary until the conversation UI is migrated.
class BasicMessage
{
public:
	enum Direction
	{
		Incoming,
		Outgoing,
		System
	};

	BasicMessage() : FDirection(Incoming) {}
	BasicMessage(const QString &AMessageId, const QString &AConversationId,
			const QString &ASender, const QString &ARecipient, const QString &ABody,
			const QDateTime &ATimestamp, const QString &AProtocol, Direction ADirection)
		: FMessageId(AMessageId), FConversationId(AConversationId), FSender(ASender),
		  FRecipient(ARecipient), FBody(ABody), FTimestamp(ATimestamp),
		  FProtocol(AProtocol), FDirection(ADirection) {}

	QString messageId() const { return FMessageId; }
	QString conversationId() const { return FConversationId; }
	QString sender() const { return FSender; }
	QString recipient() const { return FRecipient; }
	QString body() const { return FBody; }
	QDateTime timestamp() const { return FTimestamp; }
	QString protocol() const { return FProtocol; }
	QVariantMap metadata() const { return FMetadata; }
	Direction direction() const { return FDirection; }

	BasicMessage &setMessageId(const QString &AValue) { FMessageId = AValue; return *this; }
	BasicMessage &setConversationId(const QString &AValue) { FConversationId = AValue; return *this; }
	BasicMessage &setSender(const QString &AValue) { FSender = AValue; return *this; }
	BasicMessage &setRecipient(const QString &AValue) { FRecipient = AValue; return *this; }
	BasicMessage &setBody(const QString &AValue) { FBody = AValue; return *this; }
	BasicMessage &setTimestamp(const QDateTime &AValue) { FTimestamp = AValue; return *this; }
	BasicMessage &setProtocol(const QString &AValue) { FProtocol = AValue; return *this; }
	BasicMessage &setMetadata(const QVariantMap &AValue) { FMetadata = AValue; return *this; }
	BasicMessage &setDirection(Direction AValue) { FDirection = AValue; return *this; }

	bool operator==(const BasicMessage &AOther) const
	{
		return FMessageId == AOther.FMessageId && FConversationId == AOther.FConversationId &&
				FSender == AOther.FSender && FRecipient == AOther.FRecipient &&
				FBody == AOther.FBody && FTimestamp == AOther.FTimestamp &&
				FProtocol == AOther.FProtocol && FDirection == AOther.FDirection;
	}
	bool operator!=(const BasicMessage &AOther) const { return !(*this == AOther); }

private:
	QString FMessageId;
	QString FConversationId;
	QString FSender;
	QString FRecipient;
	QString FBody;
	QDateTime FTimestamp;
	QString FProtocol;
	Direction FDirection;
	QVariantMap FMetadata;
};

Q_DECLARE_METATYPE(BasicMessage)

#endif // IMESSAGE_H
