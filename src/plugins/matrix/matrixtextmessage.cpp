#include "matrixtextmessage.h"

#include <utils/matrixhtml.h>

namespace
{
QString withoutTrailingLineEndings(QString html)
{
	while (!html.isEmpty() && (html.endsWith(QLatin1Char('\r')) ||
		html.endsWith(QLatin1Char('\n'))))
		html.chop(1);
	return html;
}
}

MatrixTextMessagePayload createMatrixTextMessagePayload(
	const QString &body, const QVariantMap &metadata)
{
	const QString suppliedFormattedBody = metadata.value(QStringLiteral("formatted_body")).toString();
	const QString formattedBody = withoutTrailingLineEndings(matrixSafeHtml(suppliedFormattedBody.isEmpty()
		? matrixMarkdownToSafeHtml(body) : suppliedFormattedBody));

	MatrixTextMessagePayload payload;
	payload.content.insert(QStringLiteral("msgtype"), QStringLiteral("m.text"));
	payload.content.insert(QStringLiteral("body"), body);
	payload.content.insert(QStringLiteral("format"), QStringLiteral("org.matrix.custom.html"));
	payload.content.insert(QStringLiteral("formatted_body"), formattedBody);
	payload.content.insert(QStringLiteral("m.mentions"), QJsonObject());

	payload.localEchoMetadata = metadata;
	payload.localEchoMetadata.insert(QStringLiteral("msgtype"), QStringLiteral("m.text"));
	payload.localEchoMetadata.insert(QStringLiteral("format"), QStringLiteral("org.matrix.custom.html"));
	payload.localEchoMetadata.insert(QStringLiteral("formatted_body"), formattedBody);
	payload.localEchoMetadata.insert(QStringLiteral("m.mentions"), QVariantMap());
	return payload;
}
