#ifndef MATRIXHTML_H
#define MATRIXHTML_H

#include <QString>

// Converts user-entered Markdown or Matrix formatted_body HTML to a local,
// non-URL-loading HTML fragment suitable for chat rendering.
QString matrixMarkdownToSafeHtml(const QString &markdown);
QString matrixSafeHtml(const QString &html);
// Wraps escaped Matrix user-id tokens in safe HTML text nodes only.
QString matrixHighlightMentions(const QString &text);

#endif // MATRIXHTML_H
