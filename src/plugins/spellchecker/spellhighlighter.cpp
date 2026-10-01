#include "spellhighlighter.h"
#include "spellchecker.h"
#include "spellbackend.h"
#include <QRegularExpression>

SpellHighlighter::SpellHighlighter(QTextDocument *ADocument, IMultiUserChat *AMultiUserChat) : QSyntaxHighlighter(ADocument)
{
	FEnabled = true;
	FMultiUserChat = AMultiUserChat;
	FCharFormat.setUnderlineColor(Qt::red);
	FCharFormat.setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
}

void SpellHighlighter::setEnabled(bool AEnabled)
{
	if (FEnabled != AEnabled)
	{
		FEnabled = AEnabled;
		rehighlight();
	}
}

void SpellHighlighter::highlightBlock(const QString &AText)
{
	// Match words (minimally) excluding digits within a word
	static const QRegularExpression expression("\\b[^\\s\\d]+\\b");

	if (FEnabled)
	{
		int index = 0;
		while (true)
		{
			QRegularExpressionMatch match = expression.match(AText, index);
			if (!match.hasMatch())
				break;
			index = match.capturedStart();
			int length = match.capturedLength();
			if (!isUserNickName(match.captured()))
			{
				if (!SpellBackend::instance()->isCorrect(match.captured()))
				{
					setFormat(index, length, FCharFormat);
				}
			}
			index += length;
		}
	}
}

bool SpellHighlighter::isUserNickName(const QString &AText)
{
	return FMultiUserChat != NULL && FMultiUserChat->userByNick(AText) != NULL;
}
