#include "../../src/plugins/simplemessagestyle/decorationhelper.h"

#include <QCoreApplication>
#include <QTextBlock>
#include <QTextDocument>
#include <cstdio>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QString body = QStringLiteral("<p>Edited body</p>");
    const QString marker = DecorationHelper::renderEditedMessageHtml(body, true);
    const bool markerIsCorrect = marker.startsWith(QStringLiteral(
        "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\"><tr><td align=\"right\" style=\"font-size:6px; color:#000000; line-height:6px; margin:0; padding:0;\">(edited)</td></tr></table>")) &&
        marker.endsWith(body);
    if (!markerIsCorrect)
        std::fprintf(stderr, "FAIL: edited content must have a 6px black, right-aligned (edited) marker before its body\n");

    const bool unmodifiedIsUnchanged = DecorationHelper::renderEditedMessageHtml(body, false) == body;
    if (!unmodifiedIsUnchanged)
        std::fprintf(stderr, "FAIL: unedited content must remain unchanged\n");

    QTextDocument rendered;
    rendered.setHtml(marker);
    QTextBlock bodyBlock = rendered.begin();
    while (bodyBlock.isValid() && !bodyBlock.text().contains(QStringLiteral("Edited body")))
        bodyBlock = bodyBlock.next();
    const Qt::Alignment bodyAlignment = bodyBlock.isValid()
        ? bodyBlock.blockFormat().alignment() & Qt::AlignHorizontal_Mask
        : Qt::Alignment();
    const bool bodyRemainsLeftAligned = bodyBlock.isValid() &&
        (bodyAlignment == Qt::AlignLeft || bodyAlignment == Qt::AlignLeading);
    if (!bodyRemainsLeftAligned)
        std::fprintf(stderr, "FAIL: the edited marker must not change the message body's alignment\n");

    return markerIsCorrect && unmodifiedIsUnchanged && bodyRemainsLeftAligned ? 0 : 1;
}
