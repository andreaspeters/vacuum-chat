#ifndef IPROTOCOLROSTER_H
#define IPROTOCOLROSTER_H

#include <QList>
#include <QMetaType>
#include <QSet>
#include <QString>
#include <QtPlugin>

struct ProtocolRosterEntry
{
	QString id;
	QString name;
	QString presence;
	QString status;
	QString avatarUrl;
	QSet<QString> groups;
	bool isValid = false;
	bool hasVerificationState = false;
	bool isVerified = false;
};

struct ProtocolRoom
{
	QString id;
	QString name;
	QString subject;
	QString roomType;
	QString nickname;
	QString membership;
	QList<ProtocolRosterEntry> members;
	bool isJoined = false;
	bool isPrivate = false;
	bool isDirect = false;
	bool isAvailable = false;
	bool isEncrypted = false;
	int notificationCount = 0;
	int highlightCount = 0;
	QString avatarUrl;
	QString avatarKey;
	QString lastEventId;
	QString lastMessage;
	qint64 lastMessageTs = 0;
	QString fullyReadEventId;
	QString readEventId;
	QSet<QString> tags;
	bool markedUnread = false;
	int memberCount = -1;
};

Q_DECLARE_METATYPE(ProtocolRoom)
Q_DECLARE_METATYPE(QList<ProtocolRoom>)

class IProtocolRoster
{
public:
	virtual ~IProtocolRoster() {}
	virtual QString accountId() const = 0;
	virtual QString protocol() const = 0;
	virtual QString streamId() const = 0;
	virtual QList<ProtocolRosterEntry> entries() const = 0;
	virtual QList<ProtocolRoom> rooms() const = 0;
	virtual ProtocolRosterEntry entry(const QString &AId) const = 0;
	virtual ProtocolRoom room(const QString &AId) const = 0;
	virtual void loadRoomAvatar(const QString &roomId) const { (void)roomId; }
};

Q_DECLARE_INTERFACE(IProtocolRoster, "Vacuum.Plugin.IProtocolRoster/1.0")

#endif // IPROTOCOLROSTER_H
