#include "matrixtextmessage.h"

#include <QCoreApplication>
#include <QJsonValue>

#include <iostream>

namespace {
bool check(bool condition, const char *description)
{
	if (!condition)
		std::cerr << description << " failed\n";
	return condition;
}
}

int main(int argc, char **argv)
{
	QCoreApplication application(argc, argv);
	const QString markdown = QStringLiteral("```\ntest\n```");
	QVariantMap metadata;
	metadata.insert(QStringLiteral("txn_id"), QStringLiteral("txn-test"));
	metadata.insert(QStringLiteral("delivery_status"), QStringLiteral("sending"));
	const MatrixTextMessagePayload payload = createMatrixTextMessagePayload(markdown, metadata);

	bool passed = true;
	passed &= check(payload.content.value(QStringLiteral("msgtype")).toString() == QStringLiteral("m.text"),
		"outbound Matrix text uses msgtype m.text");
	passed &= check(payload.content.value(QStringLiteral("body")).toString() == markdown,
		"outbound body preserves the original Markdown");
	passed &= check(payload.content.value(QStringLiteral("format")).toString() ==
		QStringLiteral("org.matrix.custom.html"), "outbound format is Matrix custom HTML");
	const QString expectedHtml = QStringLiteral("<pre><code>test\n</code></pre>");
	const QString formattedBody = payload.content.value(QStringLiteral("formatted_body")).toString();

	passed &= check(formattedBody == expectedHtml,
		"fenced Markdown formatted_body has no outer trailing newline");
	passed &= check(!formattedBody.endsWith(QLatin1Char('\n')) &&
		!formattedBody.endsWith(QLatin1Char('\r')) && formattedBody.contains(QStringLiteral("test\n</code>")),
		"formatted_body trims only outer line endings and preserves code-block newlines");
	const QJsonValue mentions = payload.content.value(QStringLiteral("m.mentions"));
	passed &= check(mentions.isObject() && mentions.toObject().isEmpty(),
		"messages without mentions include an empty m.mentions object");
	passed &= check(payload.localEchoMetadata.value(QStringLiteral("txn_id")).toString() ==
		QStringLiteral("txn-test") &&
		payload.localEchoMetadata.value(QStringLiteral("format")).toString() ==
		QStringLiteral("org.matrix.custom.html") &&
		payload.localEchoMetadata.value(QStringLiteral("formatted_body")).toString() == formattedBody,
		"local echo keeps transaction data and receives formatted_body metadata");

	QVariantMap suppliedMetadata;
	suppliedMetadata.insert(QStringLiteral("formatted_body"), QStringLiteral("<p>hello</p>\r\n"));
	const MatrixTextMessagePayload suppliedPayload = createMatrixTextMessagePayload(
		QStringLiteral("hello"), suppliedMetadata);
	passed &= check(suppliedPayload.content.value(QStringLiteral("body")).toString() ==
		QStringLiteral("hello") &&
		suppliedPayload.content.value(QStringLiteral("formatted_body")).toString() ==
		QStringLiteral("<p>hello</p>"),
		"supplied formatted_body has only its terminal CR/LF removed");
	passed &= check(suppliedPayload.localEchoMetadata.value(QStringLiteral("formatted_body")).toString() ==
		suppliedPayload.content.value(QStringLiteral("formatted_body")).toString(),
		"local echo uses the normalized formatted_body");

	return passed ? 0 : 1;
}
