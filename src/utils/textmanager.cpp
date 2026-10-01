#include "textmanager.h"

#include <QTextBlock>
#include <QRegularExpression>

QString TextManager::getDocumentBody(const QTextDocument &ADocument)
{
	QRegularExpression body("<body.*>(.*)</body>", QRegularExpression::DotMatchesEverythingOption);
	QString html = ADocument.toHtml();
	QRegularExpressionMatch bodyMatch = body.match(html);
	html = bodyMatch.hasMatch() ? bodyMatch.captured(1).trimmed() : html;

	// XXX Replace <P> inserted by QTextDocument with <SPAN>
	if (html.left(3).compare("<p ", Qt::CaseInsensitive) == 0 &&
		html.right(4).compare("</p>", Qt::CaseInsensitive) == 0)
	{
		html.replace(1, 1, "span");
		html.replace(html.length() - 2, 1, "span");
	}

	return html;
}

QString TextManager::getTextFragmentHref(const QTextDocumentFragment &AFragment)
{
	QString href;

	QTextDocument doc;
	doc.setHtml(AFragment.toHtml());

	QTextBlock block = doc.firstBlock();
	while (block.isValid())
	{
		for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it)
		{
			if (it.fragment().charFormat().isAnchor())
			{
				if (href.isNull())
					href = it.fragment().charFormat().anchorHref();
				else if (href != it.fragment().charFormat().anchorHref())
					return QString();
			}
			else
			{
				return QString();
			}
		}
		block = block.next();
	}

	return href;
}

void TextManager::insertQuotedFragment(QTextCursor ACursor, const QTextDocumentFragment &AFragment)
{
	if (!AFragment.isEmpty())
	{
		ACursor.beginEditBlock();
		if (!ACursor.atBlockStart())
			ACursor.insertText("\n");
		ACursor.insertText("> ");
		ACursor.insertFragment(AFragment);
		ACursor.insertText("\n");
		ACursor.endEditBlock();
	}
}

QTextDocumentFragment TextManager::getTrimmedTextFragment(const QTextDocumentFragment &AFragment, bool APlainText)
{
	QTextDocument doc;
	QTextCursor cursor(&doc);
	if (APlainText)
	{
		QString text = AFragment.toPlainText();
		text.remove(QChar::Null);
		text.remove(QChar::ObjectReplacementCharacter);
		cursor.insertText(text);
	}
	else
	{
		cursor.insertFragment(AFragment);
	}

	cursor.movePosition(QTextCursor::Start);
	while (cursor.movePosition(QTextCursor::NextCharacter,QTextCursor::KeepAnchor) && cursor.selectedText().trimmed().isEmpty())
		cursor.removeSelectedText();

	cursor.movePosition(QTextCursor::End);
	while (cursor.movePosition(QTextCursor::PreviousCharacter,QTextCursor::KeepAnchor) && cursor.selectedText().trimmed().isEmpty())
		cursor.removeSelectedText();

	cursor.select(QTextCursor::Document);
	return cursor.selection();
}
