#include <interfaces/matrixreply.h>

#include <iostream>

namespace {
bool check(bool condition, const char *description)
{
	if (!condition)
		std::cerr << "FAIL: " << description << '\n';
	return condition;
}
}

int main()
{
	bool passed = true;
	const QJsonObject relation = MatrixReply::relation(QStringLiteral("$target-event"));
	const QJsonObject inReplyTo = relation.value(QStringLiteral("m.in_reply_to")).toObject();
	passed &= check(inReplyTo.value(QStringLiteral("event_id")).toString() ==
		QStringLiteral("$target-event"), "reply relation contains the exact target event ID");
	passed &= check(MatrixReply::relation(QString()).isEmpty(),
		"empty event IDs do not produce a relation");
	passed &= check(MatrixReply::relation(QStringLiteral("target-event")).isEmpty(),
		"malformed event IDs do not produce a relation");
	passed &= check(!MatrixReply::isEventId(QStringLiteral("$")) &&
		MatrixReply::isEventId(QStringLiteral("$target-event")),
		"validates opaque Matrix event ID shape without rewriting it");
	const QJsonObject standardReplyContent{{QStringLiteral("m.relates_to"),
		QJsonObject{{QStringLiteral("m.in_reply_to"),
			QJsonObject{{QStringLiteral("event_id"), QStringLiteral("$reply-target")}}}}}};
	passed &= check(MatrixReply::replyEventId(standardReplyContent) == QStringLiteral("$reply-target"),
		"reads the Matrix m.relates_to.m.in_reply_to event ID");
	const QJsonObject legacyReplyContent{{QStringLiteral("m.in_reply_to"),
		QJsonObject{{QStringLiteral("event_id"), QStringLiteral("$legacy-target")}}}};
	passed &= check(MatrixReply::replyEventId(legacyReplyContent) == QStringLiteral("$legacy-target"),
		"reads the legacy top-level m.in_reply_to event ID");
	const QJsonObject textContent{{QStringLiteral("msgtype"), QStringLiteral("m.text")},
		{QStringLiteral("body"), QStringLiteral("hallo")}};
	const QJsonObject replyContent = MatrixReply::applyRelation(textContent,
		QStringLiteral("$target-event"));
	passed &= check(replyContent.value(QStringLiteral("msgtype")).toString() == QStringLiteral("m.text") &&
		replyContent.value(QStringLiteral("body")).toString() == QStringLiteral("hallo"),
		"applying a reply preserves the text message fields");
	passed &= check(replyContent.value(QStringLiteral("m.relates_to")).toObject() == relation &&
		replyContent.value(QStringLiteral("m.mentions")).toObject().isEmpty(),
		"outgoing payload has nested m.in_reply_to and empty m.mentions");

	const QString html = MatrixReply::previewHtml(QStringLiteral("A <script>"),
		QStringLiteral("quoted <img src=x>\nsecond line"),
		QStringLiteral("vacuum-avatar:abc123"));
	passed &= check(html.contains(QStringLiteral("border-left:2px solid #8b5cf6")),
		"reply preview has the purple quote rule");
	passed &= check(html.contains(QStringLiteral("A &lt;script&gt;")) &&
		!html.contains(QStringLiteral("<script>")), "reply sender is escaped");
	passed &= check(html.contains(QStringLiteral("quoted &lt;img src=x&gt;<br/>second line")),
		"reply excerpt is escaped and preserves line breaks");
	passed &= check(html.contains(QStringLiteral("src=\"vacuum-avatar:abc123\"")) &&
		html.contains(QStringLiteral("width=\"18\"")) && html.contains(QStringLiteral("height=\"18\"")),
		"reply preview uses the shared cached image as a small avatar");
	passed &= check(html.contains(QStringLiteral("color:#9a9a9a")),
		"reply excerpt uses a muted text color");

	const QString plainFallback = QStringLiteral(
		"> <@andreas:matrix.aventer.biz> Nimmst auch im sport an die hände\n"
		"> zweite zitierte Zeile\n\nAhh okay");
	passed &= check(MatrixReply::stripFallbackText(plainFallback) == QStringLiteral("Ahh okay"),
		"plain Matrix fallback quote is removed while the actual reply remains");
	const QString ordinaryQuote = QStringLiteral("> an ordinary quote\n\nreply text");
	passed &= check(MatrixReply::stripFallbackText(ordinaryQuote) == ordinaryQuote,
		"ordinary user blockquotes without a Matrix sender are preserved");
	const QString htmlFallback = QStringLiteral(
		"<mx-reply><blockquote>quoted text</blockquote></mx-reply><p>Ahh okay</p>");
	passed &= check(MatrixReply::stripFallbackHtml(htmlFallback) == QStringLiteral("<p>Ahh okay</p>"),
		"formatted Matrix mx-reply fallback is removed before rendering");
	const QString incompleteHtmlFallback = QStringLiteral("<mx-reply>incomplete quote");
	passed &= check(MatrixReply::stripFallbackHtml(incompleteHtmlFallback) == incompleteHtmlFallback,
		"incomplete mx-reply markup is left intact rather than truncating the answer");
	return passed ? 0 : 1;
}
