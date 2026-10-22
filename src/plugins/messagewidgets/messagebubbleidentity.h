#ifndef MESSAGEBUBBLEIDENTITY_H
#define MESSAGEBUBBLEIDENTITY_H

#include <QObject>
#include <QString>
#include <QVariant>

namespace MessageBubbleIdentity
{
static const char MessageIdProperty[] = "vacuum.messageId";
static const char EventIdProperty[] = "vacuum.eventId";

inline void set(QObject *AObject, const QString &AMessageId, const QString &AEventId)
{
	if (!AObject)
		return;
	AObject->setProperty(MessageIdProperty, AMessageId);
	AObject->setProperty(EventIdProperty, AEventId);
}

inline QString messageId(const QObject *AObject)
{
	return AObject ? AObject->property(MessageIdProperty).toString() : QString();
}

inline QString eventId(const QObject *AObject)
{
	return AObject ? AObject->property(EventIdProperty).toString() : QString();
}

inline QObject *identityAncestor(QObject *AObject, const QObject *AStopBefore)
{
	for (QObject *candidate = AObject; candidate && candidate != AStopBefore;
		candidate = candidate->parent())
		if (!messageId(candidate).isEmpty())
			return candidate;
	return nullptr;
}

inline bool shouldUseWholeBubbleText(bool AHasAnchor, bool AWordSelected, bool AHasContent)
{
	return !AHasAnchor && !AWordSelected && AHasContent;
}

inline bool shouldUseTextRangeFallback(bool AHasIdentityBubble, bool AFoundBubble)
{
	return !AHasIdentityBubble && !AFoundBubble;
}
}

#endif // MESSAGEBUBBLEIDENTITY_H
