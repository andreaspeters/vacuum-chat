#ifndef IPROTOCOLMESSAGING_H
#define IPROTOCOLMESSAGING_H

#include <QString>
#include <QList>

#include <QtPlugin>
#include <interfaces/imessage.h>
#include <interfaces/iprotocolpresence.h>
#include <interfaces/identity.h>
#include <utils/jid.h>

class IProtocolMessaging
{
public:
    virtual ~IProtocolMessaging() {}
    virtual AccountId streamId() const = 0;
    virtual QString protocol() const { return QString(); }
    virtual QString formatEmoticonForSending(const QString &iconKey) const
    { return iconKey; }
    virtual QString formatEmoticonsForSending(const QString &text) const
    { return text; }
    virtual QString formatEmoticonsForDisplay(const QString &text) const
    { return text; }
    virtual QString emojiPickerType() const
    { return QString(); }
    virtual bool conversationIdForAddress(const Jid &address, ConversationId &conversationId) const = 0;
    virtual Jid addressForConversation(const ConversationId &conversationId) const = 0;
    virtual QString conversationDisplayName(const ConversationId &AConversationId) const
    { (void)AConversationId; return QString(); }
    virtual QString conversationAvatarPath(const ConversationId &AConversationId) const
    { (void)AConversationId; return QString(); }
    virtual QString userAvatarPath(const ConversationId &AConversationId, const UserId &AUserId) const
    { (void)AConversationId; (void)AUserId; return QString(); }
    virtual QString userAvatarKey(const ConversationId &AConversationId, const UserId &AUserId) const
    { (void)AConversationId; (void)AUserId; return QString(); }
    virtual bool conversationOnline(const Jid &address) const { (void)address; return false; }
    virtual bool sendMessage(const BasicMessage &message) = 0;
    virtual bool supportsReactions(const ConversationId &conversationId) const
    { (void)conversationId; return false; }
    virtual bool supportsReplies(const ConversationId &conversationId) const
    { (void)conversationId; return false; }
    virtual bool providesAvatarUpdateSignals() const { return false; }
    virtual bool providesHistoryLoadedSignals() const { return false; }
    virtual bool supportsConversationMedia() const { return false; }
    virtual bool supportsFileTransfer() const { return false; }
    virtual bool isEventIdLike(const QString &eventId) const
    { (void)eventId; return false; }
    virtual QString replyPreviewHtml(const QString &senderName, const QString &excerpt, const QString &avatarResourceUrl) const
    { (void)senderName; (void)excerpt; (void)avatarResourceUrl; return QString(); }
    virtual QString stripReplyFallback(const QString &body, const QString &formatType) const
    { (void)body; (void)formatType; return body; }
    virtual QString sanitizeHtml(const QString &html) const
    { return html; }
    virtual QString highlightMentions(const QString &text) const
    { return text; }
    virtual bool sendReaction(const ConversationId &conversationId, const MessageId &eventId,
        const QString &key)
    { (void)conversationId; (void)eventId; (void)key; return false; }
    virtual bool supportsTyping(const ConversationId &conversationId) const
    { (void)conversationId; return false; }
    virtual bool supportsRoomInvites() const { return false; }
    virtual bool inviteUserToRoom(const ConversationId &roomId, const UserId &userId)
    { (void)roomId; (void)userId; return false; }
    virtual void setTyping(const ConversationId &conversationId, ProtocolTypingStatus status)
    { (void)conversationId; (void)status; }
    virtual QList<BasicMessage> conversationHistory(const ConversationId &conversationId) const
    { (void)conversationId; return QList<BasicMessage>(); }
    virtual void setActiveConversation(const ConversationId &conversationId) const
    { (void)conversationId; }
    virtual void loadConversationAvatars(const ConversationId &conversationId) const
    { (void)conversationId; }
    virtual void loadUserAvatar(const ConversationId &conversationId, const UserId &userId) const
    { (void)conversationId; (void)userId; }
    virtual void loadConversationMedia(const BasicMessage &message) const
    { (void)message; }
    virtual bool markConversationRead(const ConversationId &conversationId, const MessageId &eventId)
    { (void)conversationId; (void)eventId; return false; }
    virtual MessageId latestConversationEventId(const ConversationId &conversationId) const
    { (void)conversationId; return QString(); }
};

Q_DECLARE_INTERFACE(IProtocolMessaging, "Vacuum.Plugin.IProtocolMessaging/1.0")

#endif // IPROTOCOLMESSAGING_H
