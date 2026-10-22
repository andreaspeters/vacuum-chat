#include "messagebubbleidentity.h"
#include "../../src/interfaces/matrixreply.h"

#include <QObject>
#include <QJsonObject>
#include <QString>

#include <cassert>

int main()
{
	QObject viewport;
	QObject bubbleFrame(&viewport);
	QObject bubbleContent(&bubbleFrame);
	MessageBubbleIdentity::set(&bubbleFrame, QStringLiteral("bubble-id"), QStringLiteral("$bubble-event"));
	MessageBubbleIdentity::set(&viewport, QStringLiteral("outside-id"), QStringLiteral("$outside-event"));
	assert(MessageBubbleIdentity::identityAncestor(&bubbleContent, &viewport) == &bubbleFrame);
	assert(MessageBubbleIdentity::identityAncestor(&viewport, nullptr) == &viewport);
	assert(MessageBubbleIdentity::shouldUseWholeBubbleText(false, false, true));
	assert(!MessageBubbleIdentity::shouldUseWholeBubbleText(false, true, true));
	assert(!MessageBubbleIdentity::shouldUseWholeBubbleText(true, false, true));
	assert(!MessageBubbleIdentity::shouldUseWholeBubbleText(false, false, false));

	assert(MessageBubbleIdentity::shouldUseTextRangeFallback(false, false));
	assert(!MessageBubbleIdentity::shouldUseTextRangeFallback(true, false));
	assert(!MessageBubbleIdentity::shouldUseTextRangeFallback(false, true));

	QObject bubble;
	MessageBubbleIdentity::set(&bubble, QStringLiteral("display-id"), QStringLiteral("$event-id"));

	assert(MessageBubbleIdentity::messageId(&bubble) == QStringLiteral("display-id"));
	assert(MessageBubbleIdentity::eventId(&bubble) == QStringLiteral("$event-id"));
	const QJsonObject body{{QStringLiteral("body"), QStringLiteral("reply text")}};
	const QJsonObject outgoing = MatrixReply::applyRelation(body,
		MessageBubbleIdentity::eventId(&bubble));
	assert(outgoing.value(QStringLiteral("m.relates_to")).toObject()
		.value(QStringLiteral("m.in_reply_to")).toObject()
		.value(QStringLiteral("event_id")).toString() == QStringLiteral("$event-id"));
	assert(MatrixReply::replyEventId(outgoing) == QStringLiteral("$event-id"));

	MessageBubbleIdentity::set(&bubble, QStringLiteral("display-id"), QString());
	assert(MessageBubbleIdentity::messageId(&bubble) == QStringLiteral("display-id"));
	assert(MessageBubbleIdentity::eventId(&bubble).isEmpty());
	assert(!MatrixReply::applyRelation(body, MessageBubbleIdentity::eventId(&bubble))
		.contains(QStringLiteral("m.relates_to")));
	MessageBubbleIdentity::set(&bubble, QStringLiteral("display-id"), QStringLiteral("$server-event"));
	assert(MessageBubbleIdentity::eventId(&bubble) == QStringLiteral("$server-event"));
	assert(MessageBubbleIdentity::eventId(nullptr).isEmpty());
	return 0;
}
