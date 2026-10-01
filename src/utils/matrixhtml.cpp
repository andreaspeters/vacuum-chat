#include "matrixhtml.h"

#include <QRegularExpression>
#include <QTextDocument>
#include <QTextCursor>
#include <QTextDocumentFragment>
#include <QStringList>

namespace {

QString safeMarkdownFragment(const QString &markdown);

QString sanitize(const QString &input)
{
	QString html = input;
	QStringList inlineImages;
	const QRegularExpression validImage(
		QStringLiteral("<img\\s+src=[\\\"'](data:image/(?:png|gif|jpe?g|webp);base64,[A-Za-z0-9+/=]+)[\\\"']\\s+alt=[\\\"']([^\\\"']*)[\\\"']\\s*/?>"),
		QRegularExpression::CaseInsensitiveOption);
	QRegularExpressionMatch imageMatch = validImage.match(html);
	while (imageMatch.hasMatch()) {
		const QString placeholder = QStringLiteral("MATRIX_INLINE_IMAGE_%1").arg(inlineImages.size());
		inlineImages.append(QStringLiteral("<img src=\"%1\" alt=\"%2\" />")
			.arg(imageMatch.captured(1), imageMatch.captured(2).toHtmlEscaped()));
		html.replace(imageMatch.capturedStart(), imageMatch.capturedLength(), placeholder);
		imageMatch = validImage.match(html, imageMatch.capturedStart() + placeholder.size());
	}
	html.remove(QRegularExpression(QStringLiteral("<img\\b[^>]*>"),
		QRegularExpression::CaseInsensitiveOption));
	static const QRegularExpression dangerous(
		QStringLiteral("<(script|iframe|object|embed|link|style|form|video|audio|svg)\\b[^>]*>.*?</\\1\\s*>") ,
		QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
	html.remove(dangerous);
	html.remove(QRegularExpression(QStringLiteral("<!--.*?-->"),
		QRegularExpression::DotMatchesEverythingOption));

	// Only text-formatting tags are allowed. All attributes are removed, so no
	// href/src/style/URL can trigger a remote resource or script.
	static const QRegularExpression nonAllowed(
		QStringLiteral("</?(?!/?(?:br|p|pre|code|blockquote|ul|ol|li|strong|em|b|i|u|s|span|table|thead|tbody|tr|th|td)(?:\\s|/?>))[^>]*>"),
		QRegularExpression::CaseInsensitiveOption);
	html.remove(nonAllowed);
	static const QRegularExpression attributes(
		QStringLiteral("<(br|p|pre|code|blockquote|ul|ol|li|strong|em|b|i|u|s|span|table|thead|tbody|tr|th|td)(?:\\s+[^>]*)?>"),
		QRegularExpression::CaseInsensitiveOption);
	html.replace(attributes, QStringLiteral("<\\1>"));
	for (int index = 0; index < inlineImages.size(); ++index)
		html.replace(QStringLiteral("MATRIX_INLINE_IMAGE_%1").arg(index), inlineImages.at(index));
	return html;
}

QStringList tableCells(const QString &line)
{
	QString value = line.trimmed();
	if (value.startsWith(QLatin1Char('|')))
		value.remove(0, 1);
	if (value.endsWith(QLatin1Char('|')))
		value.chop(1);
	return value.split(QLatin1Char('|'));
}

bool isTableSeparator(const QString &line)
{
	const QStringList cells = tableCells(line);
	if (cells.isEmpty())
		return false;
	const QRegularExpression separator(QStringLiteral("^\\s*:?-{3,}:?\\s*$"));
	for (const QString &cell : cells)
		if (!separator.match(cell).hasMatch())
			return false;
	return true;
}

QString tableHtml(const QStringList &header, const QList<QStringList> &rows)
{
	QString html = QStringLiteral("<table><thead><tr>");
	for (const QString &cell : header)
		html += QStringLiteral("<th>") + safeMarkdownFragment(cell.trimmed()) + QStringLiteral("</th>");
	html += QStringLiteral("</tr></thead><tbody>");
	for (const QStringList &row : rows) {
		html += QStringLiteral("<tr>");
		for (int index = 0; index < header.size(); ++index)
			html += QStringLiteral("<td>") + safeMarkdownFragment(
				index < row.size() ? row.at(index).trimmed() : QString()) + QStringLiteral("</td>");
		html += QStringLiteral("</tr>");
	}
	return html + QStringLiteral("</tbody></table>");
}

QString safeMarkdownFragment(const QString &markdown)
{
	QTextDocument document;
	document.setMarkdown(markdown);
	QTextCursor cursor(&document);
	cursor.select(QTextCursor::Document);
	return sanitize(QTextDocumentFragment(cursor).toHtml());
}

}

QString matrixMarkdownToSafeHtml(const QString &markdown)
{
	QStringList lines = markdown.split(QRegularExpression(QStringLiteral("\\r?\\n")));
	QStringList prepared;
	QMap<QString, QString> tables;
	for (int index = 0; index < lines.size(); ++index) {
		if (index + 1 < lines.size() && lines.at(index).contains(QLatin1Char('|')) &&
			isTableSeparator(lines.at(index + 1))) {
			const QString placeholder = QStringLiteral("MATRIX_TABLE_%1").arg(tables.size());
			const QStringList header = tableCells(lines.at(index));
			QList<QStringList> rows;
			index += 2;
			while (index < lines.size() && lines.at(index).contains(QLatin1Char('|')) &&
				!lines.at(index).trimmed().isEmpty())
				rows.append(tableCells(lines.at(index++)));
			--index;
			tables.insert(placeholder, tableHtml(header, rows));
			prepared.append(placeholder);
		} else {
			prepared.append(lines.at(index));
		}
	}
	QTextDocument document;
	document.setMarkdown(prepared.join(QLatin1Char('\n')));
	QTextCursor cursor(&document);
	cursor.select(QTextCursor::Document);
	QString fragment = sanitize(QTextDocumentFragment(cursor).toHtml());
	for (auto it = tables.constBegin(); it != tables.constEnd(); ++it) {
		fragment.replace(QStringLiteral("<p>") + it.key() + QStringLiteral("</p>"), it.value());
		fragment.replace(it.key(), it.value());
	}
	fragment.replace(QStringLiteral("<table>"),
		QStringLiteral("<table style=\"border-collapse:collapse; margin:6px 0;\">"));
	fragment.replace(QStringLiteral("<th>"),
		QStringLiteral("<th style=\"border:1px solid #888; padding:4px 8px; background:#e8eaed;\">"));
	fragment.replace(QStringLiteral("<td>"),
		QStringLiteral("<td style=\"border:1px solid #aaa; padding:4px 8px;\">"));
	fragment.replace(QStringLiteral("<pre>"),
		QStringLiteral("<pre style=\"background:#202124; color:#e8eaed; padding:8px; "
			"border-radius:6px; white-space:pre-wrap;\">"));
	fragment.replace(QStringLiteral("<code>"),
		QStringLiteral("<code style=\"font-family:monospace;\">"));
	return fragment;
}

QString matrixSafeHtml(const QString &html)
{
	QString converted = html;
	const QRegularExpression markdownCode(
		QStringLiteral("<pre><code[^>]*class=[\\\"']([^\\\"']*language-markdown[^\\\"']*)[\\\"'][^>]*>(.*?)</code></pre>"),
		QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
	for (QRegularExpressionMatch match = markdownCode.match(converted); match.hasMatch();
		match = markdownCode.match(converted)) {
		const QString codeHtml = match.captured(2);
		QTextDocument codeDocument;
		codeDocument.setHtml(codeHtml);
		const QString markdown = codeDocument.toPlainText();
		if (markdown.contains(QLatin1Char('|')) && isTableSeparator(
			markdown.split(QRegularExpression(QStringLiteral("\\r?\\n"))).value(1)))
			converted.replace(match.capturedStart(), match.capturedLength(), matrixMarkdownToSafeHtml(markdown));
		else
			break;
	}
	return sanitize(converted);
}
