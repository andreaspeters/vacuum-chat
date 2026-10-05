#include "matrixhtml.h"

#include <QCoreApplication>
#include <QColor>
#include <QString>
#include <QTextCursor>
#include <QTextDocument>

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
    const QString token = QStringLiteral("&lt;@andreas:matrix.aventer.biz&gt;");
    const QString marker = QStringLiteral("<span class=\"matrix-mention\"");
    bool passed = true;

    const QString plain = matrixHighlightMentions(QStringLiteral("Hello ") + token +
        QStringLiteral("; welcome."));
    passed &= check(plain.count(marker) == 1 &&
        plain.contains(marker) && plain.contains(token + QStringLiteral("</span>")),
        "plain escaped text highlights only the mention token");
    passed &= check(plain.startsWith(QStringLiteral("Hello ")) &&
        plain.endsWith(QStringLiteral("; welcome.")),
        "plain text outside the mention remains unchanged");
    QTextDocument renderedPlain;
    renderedPlain.setHtml(plain);
    const QString displayedToken = QStringLiteral("<@andreas:matrix.aventer.biz>");
    const int mentionPosition = renderedPlain.toPlainText().indexOf(displayedToken);
    QTextCursor mentionCursor(&renderedPlain);
    if (mentionPosition >= 0) {
        mentionCursor.setPosition(mentionPosition);
        mentionCursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor,
            displayedToken.size());
    }
    passed &= check(mentionPosition >= 0 &&
        mentionCursor.charFormat().background().color() == QColor("#604a8b") &&
        mentionCursor.charFormat().foreground().color() == QColor("#ffffff"),
        "rendered mention has a distinct foreground and background");

    const QString formattedInput = QStringLiteral("<p>Hi ") + token +
        QStringLiteral(" <em>there</em></p>");
    const QString formatted = matrixHighlightMentions(matrixSafeHtml(formattedInput));
    passed &= check(formatted.count(marker) == 1 &&
        formatted.contains(QStringLiteral("<em>there</em>")),
        "sanitized formatted text preserves formatting and highlights the mention");

    const QString withAttribute = QStringLiteral("<a href=\"https://example.org/?u=%1\">%1</a>").arg(token);
    const QString highlightedAttribute = matrixHighlightMentions(withAttribute);
    passed &= check(highlightedAttribute.count(marker) == 1 &&
        highlightedAttribute.startsWith(QStringLiteral("<a href=\"https://example.org/?u=%1\">").arg(token)),
        "mention-like text in an HTML attribute is not highlighted");

    const QString withoutMention = QStringLiteral("Just an ordinary message.");
    passed &= check(matrixHighlightMentions(withoutMention) == withoutMention,
        "messages without mentions remain unchanged");

    const QString shortMention = QStringLiteral("&lt;@benutzerid&gt;");
    const QString highlightedShortMention = matrixHighlightMentions(shortMention);
    passed &= check(highlightedShortMention.contains(marker) &&
        highlightedShortMention.contains(shortMention + QStringLiteral("</span>")),
        "short user-id mention tokens are highlighted");

    return passed ? 0 : 1;
}
