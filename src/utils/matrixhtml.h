#ifndef MATRIXHTML_H
#define MATRIXHTML_H

#include <QString>

// Converts user-entered Markdown or Matrix formatted_body HTML to a local,
// non-URL-loading HTML fragment suitable for chat rendering.
QString matrixMarkdownToSafeHtml(const QString &markdown);
QString matrixSafeHtml(const QString &html);

#endif // MATRIXHTML_H
