#ifndef IPROTOCOLNOTIFICATIONS_H
#define IPROTOCOLNOTIFICATIONS_H

#include <QDateTime>
#include <QList>
#include <QString>
#include <QtPlugin>

struct ProtocolNotification
{
	enum Kind
	{
		Info,
		Message,
		Mention,
		Transfer,
		Invite,
		Reaction,
		Call,
		Membership,
		Verification
	};

	QString id;
	QString accountId;
	QString streamId;
	QString conversationId;
	QString title;
	QString body;
	QString protocol;
	QDateTime timestamp;
	Kind kind = Info;
};

struct ProtocolFileTransfer
{
	enum State
	{
		Pending,
		Running,
		Finished,
		Failed,
		Cancelled
	};

	QString id;
	QString accountId;
	QString conversationId;
	QString name;
	QString mimeType;
	QString source;
	QString destination;
	qint64 size = 0;
	qint64 progress = 0;
	State state = Pending;
};

class IProtocolNotifications
{
public:
	// QObject-backed providers expose protocolNotificationsChanged() on their
	// plugin object whenever this snapshot changes.
	virtual ~IProtocolNotifications() {}
	virtual QString accountId() const = 0;
	virtual QString protocol() const = 0;
	virtual QList<ProtocolNotification> notifications() const = 0;
	virtual void appendNotification(const ProtocolNotification &ANotification) = 0;
	virtual void removeNotification(const QString &AId) = 0;
};

class IProtocolFileTransfer
{
public:
	virtual ~IProtocolFileTransfer() {}
	virtual QString accountId() const = 0;
	virtual QString protocol() const = 0;
	virtual QList<ProtocolFileTransfer> transfers() const = 0;
	virtual ProtocolFileTransfer transfer(const QString &AId) const = 0;
};

Q_DECLARE_INTERFACE(IProtocolNotifications, "Vacuum.Plugin.IProtocolNotifications/1.0")
Q_DECLARE_INTERFACE(IProtocolFileTransfer, "Vacuum.Plugin.IProtocolFileTransfer/1.0")

#endif // IPROTOCOLNOTIFICATIONS_H
