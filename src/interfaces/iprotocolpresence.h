#ifndef IPROTOCOLPRESENCE_H
#define IPROTOCOLPRESENCE_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QDateTime>
#include <interfaces/identity.h>

/* Protocol-neutral presence and typing primitives.
 * Independent of chat type (XMPP/JID-specific or Matrix-specific).
 */

/* Presence enumeration - neutral across protocols */
enum ProtocolPresence {
    PresenceUnknown = 0,
    PresenceOnline = 1,
    PresenceAway = 2,
    PresenceDND = 3,
    PresenceXA = 4,
    PresenceOffline = 5
};

Q_DECLARE_METATYPE(ProtocolPresence)

/* Typing status enumeration - neutral across protocols */
enum ProtocolTypingStatus {
    TypingStatusNotTyping = 0,
    TypingStatusComposing = 1,
    TypingStatusPaused = 2
};

Q_DECLARE_METATYPE(ProtocolTypingStatus)

/* Presence update value struct - protocol-neutral */
struct ProtocolPresenceUpdate {
    QString accountId;       /* Account identifier (e.g., protocol-specific ID) */
    QString userId;          /* User identifier (e.g., JID/Matrix user ID) */
    ProtocolPresence presence;  /* Current presence state */
    QString status;          /* Free-form status text */
    qint64 lastActiveAgo;    /* Milliseconds since last activity (0 if currently active) */
    bool currentlyActive;    /* True if actively interacting */

    ProtocolPresenceUpdate()
        : presence(PresenceUnknown), lastActiveAgo(0), currentlyActive(false) {}

    ProtocolPresenceUpdate(const QString &account, const QString &user,
                           ProtocolPresence p, const QString &statusText,
                           qint64 lastActive, bool active)
        : accountId(account), userId(user), presence(p), status(statusText),
          lastActiveAgo(lastActive), currentlyActive(active) {}

    bool isValid() const {
        return !accountId.isEmpty() && !userId.isEmpty();
    }

    QString toString() const {
        QString statusText = currentlyActive ? " (active)" : QString(" (%1 ago)").arg(lastActiveAgo);
        return QString("%1:%2 %3%4").arg(accountId).arg(userId)
                                   .arg((int)presence).arg(statusText);
    }
};

Q_DECLARE_METATYPE(ProtocolPresenceUpdate)

/* Typing update value struct - protocol-neutral */
struct ProtocolTypingUpdate {
    QString accountId;       /* Account identifier */
    QString conversationId;  /* Conversation identifier (room/thread ID) */
    QVector<QString> userIds; /* Users currently typing in this conversation */

    ProtocolTypingUpdate()
        : accountId(), conversationId() {}

    ProtocolTypingUpdate(const QString &account, const QString &conversation,
                         const QVector<QString> &typingUsers)
        : accountId(account), conversationId(conversation), userIds(typingUsers) {}

    bool hasTypingUsers() const {
        return !userIds.isEmpty();
    }

    int typingCount() const {
        return userIds.size();
    }

    QString toString() const {
        QStringList userStrs;
        for (const QString &user: userIds) {
            userStrs.append(user);
        }
        return QString("%1:%2 [%3]").arg(accountId, conversationId).arg(userStrs.join(", "));
    }
};

Q_DECLARE_METATYPE(ProtocolTypingUpdate)

/* Legacy protocol interface for backward compatibility */
class IProtocolPresence
{
public:
    virtual ~IProtocolPresence() {}
    virtual QObject *instance() = 0;
    virtual QString streamId() const =0;
    virtual AccountId accountId() const { return AccountId(); }
    virtual int show() const =0;
    virtual QString status() const =0;
    virtual bool setPresence(int AShow, const QString &AStatus) =0;
};

Q_DECLARE_INTERFACE(IProtocolPresence, "Vacuum.Plugin.IProtocolPresence/1.0")

#endif