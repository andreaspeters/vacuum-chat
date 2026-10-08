#include "viewwidget.h"

#include <interfaces/messagedecorationpolicy.h>
#include <QTextFrame>
#include <QTextTable>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QTextDocumentFragment>

ViewWidget::ViewWidget(IMessageWidgets *AMessageWidgets, const Jid &AStreamJid, const Jid &AContactJid, QWidget *AParent) : QWidget(AParent)
{
	ui.setupUi(this);
	setAcceptDrops(true);

	QVBoxLayout *layout = new QVBoxLayout(ui.wdtViewer);
	layout->setContentsMargins(0,0,0,0);

	FMessageStyle = NULL;
	FMessageProcessor = NULL;
	FMessageWidgets = AMessageWidgets;

	FStreamJid = AStreamJid;
	FContactJid = AContactJid;
	FStyleWidget = NULL;

	initialize();
}

ViewWidget::ViewWidget(IMessageWidgets *AMessageWidgets, const AccountId &AAccountId, const ConversationId &AConversationId, QWidget *AParent) : QWidget(AParent)
{
	ui.setupUi(this);
	setAcceptDrops(true);
	QVBoxLayout *layout = new QVBoxLayout(ui.wdtViewer);
	layout->setContentsMargins(0,0,0,0);
	FMessageStyle = NULL;
	FMessageProcessor = NULL;
	FMessageWidgets = AMessageWidgets;
	FAccountId = AAccountId;
	FConversationId = AConversationId;
	FStyleWidget = NULL;
	initialize();
}

ViewWidget::~ViewWidget()
{

}

void ViewWidget::setStreamJid(const Jid &AStreamJid)
{
	if (AStreamJid != FStreamJid)
	{
		Jid before = FStreamJid;
		FStreamJid = AStreamJid;
		emit streamJidChanged(before);
	}
}

void ViewWidget::setContactJid(const Jid &AContactJid)
{
	if (AContactJid != FContactJid)
	{
		Jid before = FContactJid;
		FContactJid = AContactJid;
		emit contactJidChanged(before);
	}
}

QWidget *ViewWidget::styleWidget() const
{
	return FStyleWidget;
}

IMessageStyle *ViewWidget::messageStyle() const
{
	return FMessageStyle;
}

void ViewWidget::setMessageStyle(IMessageStyle *AStyle, const IMessageStyleOptions &AOptions)
{
	if (FMessageStyle != AStyle)
	{
		FMessageRanges.clear();
		FMessageDecorations.clear();
		IMessageStyle *before = FMessageStyle;
		FMessageStyle = AStyle;
		if (before)
		{
			disconnect(before->instance(),SIGNAL(contentAppended(QWidget *, const QString &, const IMessageContentOptions &)),
				this, SLOT(onContentAppended(QWidget *, const QString &, const IMessageContentOptions &)));
			disconnect(before->instance(),SIGNAL(urlClicked(QWidget *, const QUrl &)),this,SLOT(onUrlClicked(QWidget *, const QUrl &)));
			disconnect(FStyleWidget,SIGNAL(customContextMenuRequested(const QPoint &)),this,SLOT(onCustomContextMenuRequested(const QPoint &)));
			ui.wdtViewer->layout()->removeWidget(FStyleWidget);
			FStyleWidget->deleteLater();
			FStyleWidget = NULL;
		}
		if (FMessageStyle)
		{
			FStyleWidget = FMessageStyle->createWidget(AOptions,ui.wdtViewer);
			FStyleWidget->setContextMenuPolicy(Qt::CustomContextMenu);
			connect(FStyleWidget,SIGNAL(customContextMenuRequested(const QPoint &)),SLOT(onCustomContextMenuRequested(const QPoint &)));
			connect(FMessageStyle->instance(),SIGNAL(contentAppended(QWidget *, const QString &, const IMessageContentOptions &)),
				SLOT(onContentAppended(QWidget *, const QString &, const IMessageContentOptions &)));
			connect(FMessageStyle->instance(),SIGNAL(urlClicked(QWidget *, const QUrl &)),SLOT(onUrlClicked(QWidget *, const QUrl &)));
			ui.wdtViewer->layout()->addWidget(FStyleWidget);
		}
		emit messageStyleChanged(before,AOptions);
	}
}

void ViewWidget::appendHtml(const QString &AHtml, const IMessageContentOptions &AOptions)
{
	if (FMessageStyle)
	{
		QTextEdit *view = qobject_cast<QTextEdit *>(FStyleWidget);
		const int start = view ? view->document()->characterCount() - 1 : -1;
		FMessageStyle->appendContent(FStyleWidget,AHtml,AOptions);
		if (view && !AOptions.messageId.isEmpty())
			FMessageRanges.insert(AOptions.messageId, qMakePair(start, view->document()->characterCount() - 1));
	}
}

bool ViewWidget::replaceMessage(const QString &AMessageId, const QString &AHtml)
{
	QTextEdit *view = qobject_cast<QTextEdit *>(FStyleWidget);
	if (!view || !FMessageRanges.contains(AMessageId))
		return false;
	const QStringList decorationIds = FMessageDecorations.value(AMessageId).keys();
	for (const QString &decorationId : decorationIds)
		setMessageDecoration(AMessageId, decorationId, QString());
	const QPair<int, int> range = FMessageRanges.value(AMessageId);
	QTextCursor cursor(view->document());
	cursor.setPosition(range.first);
	cursor.setPosition(range.second, QTextCursor::KeepAnchor);
	cursor.insertHtml(AHtml);
	const int newEnd = cursor.position();
	const int delta = newEnd - range.second;
	for (auto it = FMessageRanges.begin(); it != FMessageRanges.end(); ++it)
	{
		if (it.key() == AMessageId)
			it.value() = qMakePair(range.first, newEnd);
		else if (it.value().first >= range.second)
			it.value() = qMakePair(it.value().first + delta, it.value().second + delta);
		else if (it.value().second >= range.second)
			it.value().second += delta;
	}
	for (auto messageIt = FMessageDecorations.begin(); messageIt != FMessageDecorations.end(); ++messageIt)
		for (auto decorationIt = messageIt.value().begin(); decorationIt != messageIt.value().end(); ++decorationIt)
			if (decorationIt.value().first >= range.second)
				decorationIt.value() = qMakePair(decorationIt.value().first + delta,
					decorationIt.value().second + delta);
			else if (decorationIt.value().second >= range.second)
				decorationIt.value().second += delta;
	return true;
}

bool ViewWidget::setMessageDecoration(const QString &AMessageId, const QString &ADecorationId,
	const QString &AHtml)
{
	QTextEdit *view = qobject_cast<QTextEdit *>(FStyleWidget);
	if (!view || AMessageId.isEmpty() || ADecorationId.isEmpty() ||
		!FMessageRanges.contains(AMessageId))
		return false;

	QMap<QString, QPair<int, int>> &decorations = FMessageDecorations[AMessageId];
	if (decorations.contains(ADecorationId))
	{
		const QPair<int, int> range = decorations.take(ADecorationId);
		QTextCursor cursor(view->document());
		cursor.setPosition(range.first);
		cursor.setPosition(range.second, QTextCursor::KeepAnchor);
		cursor.removeSelectedText();
		const int delta = cursor.position() - range.second;
		for (auto it = FMessageRanges.begin(); it != FMessageRanges.end(); ++it)
			if (it.value().first >= range.second)
				it.value() = qMakePair(it.value().first + delta, it.value().second + delta);
			else if (it.value().second >= range.second)
				it.value().second += delta;
		for (auto messageIt = FMessageDecorations.begin(); messageIt != FMessageDecorations.end(); ++messageIt)
			for (auto decorationIt = messageIt.value().begin(); decorationIt != messageIt.value().end(); ++decorationIt)
				if (decorationIt.value().first >= range.second)
					decorationIt.value() = qMakePair(decorationIt.value().first + delta,
						decorationIt.value().second + delta);
				else if (decorationIt.value().second >= range.second)
					decorationIt.value().second += delta;
		if (decorations.isEmpty())
			FMessageDecorations.remove(AMessageId);
	}
	const bool styleHandled = FMessageStyle &&
		FMessageStyle->setMessageDecoration(FStyleWidget, AMessageId, ADecorationId, AHtml);
	if (MessageDecorationPolicy::routeForStyleResult(styleHandled) ==
		MessageDecorationPolicy::StyleWidgetHandled)
	{
		if (!AHtml.isEmpty())
		{
			const int anchor = FMessageRanges.value(AMessageId).second;
			FMessageDecorations[AMessageId].insert(ADecorationId, qMakePair(anchor, anchor));
		}
		return true;
	}
	if (AHtml.isEmpty())
		return true;

	const int start = FMessageRanges.value(AMessageId).second;
	QTextCursor cursor(view->document());
	cursor.setPosition(start);
	cursor.insertHtml(AHtml);
	const int end = cursor.position();
	const int delta = end - start;
	if (delta <= 0)
	{
		if (FMessageStyle)
			FMessageStyle->setMessageDecoration(FStyleWidget, AMessageId, ADecorationId, QString());
		return false;
	}
	for (auto it = FMessageRanges.begin(); it != FMessageRanges.end(); ++it)
		if (it.value().first >= start)
			it.value() = qMakePair(it.value().first + delta, it.value().second + delta);
		else if (it.value().second >= start)
			it.value().second += delta;
	for (auto messageIt = FMessageDecorations.begin(); messageIt != FMessageDecorations.end(); ++messageIt)
		for (auto decorationIt = messageIt.value().begin(); decorationIt != messageIt.value().end(); ++decorationIt)
			if (decorationIt.value().first >= start)
				decorationIt.value() = qMakePair(decorationIt.value().first + delta,
					decorationIt.value().second + delta);
			else if (decorationIt.value().second >= start)
				decorationIt.value().second += delta;
	FMessageRanges[AMessageId].second = end;
	FMessageDecorations[AMessageId].insert(ADecorationId, qMakePair(start, end));
	return true;
}

void ViewWidget::appendText(const QString &AText, const IMessageContentOptions &AOptions)
{
	Message message;
	message.setBody(AText);
	appendMessage(message,AOptions);
}

void ViewWidget::appendMessage(const Message &AMessage, const IMessageContentOptions &AOptions)
{
	QTextDocument doc;
	if (FMessageProcessor)
		FMessageProcessor->messageToText(&doc,AMessage);
	else
		doc.setPlainText(AMessage.body());

	// "/me" command
	IMessageContentOptions options = AOptions;
	if (AOptions.kind==IMessageContentOptions::KindMessage && !AOptions.senderName.isEmpty())
	{
		QTextCursor cursor(&doc);
		cursor.movePosition(QTextCursor::NextCharacter,QTextCursor::KeepAnchor,4);
		if (cursor.selectedText() == "/me ")
		{
			options.kind = IMessageContentOptions::KindMeCommand;
			cursor.removeSelectedText();
		}
	}

	appendHtml(TextManager::getDocumentBody(doc),options);
}

void ViewWidget::contextMenuForView(const QPoint &APosition, const QTextDocumentFragment &ASelection, Menu *AMenu)
{
	QTextEdit *view = qobject_cast<QTextEdit *>(FStyleWidget);
	if (view && AMenu)
	{
		const int position = view->cursorForPosition(APosition).position();
		for (auto it = FMessageRanges.constBegin(); it != FMessageRanges.constEnd(); ++it)
			if (position >= it.value().first && position <= it.value().second)
			{
				AMenu->setProperty("vacuum.messageId", it.key());
				break;
			}
	}
	emit viewContextMenu(APosition,ASelection,AMenu);
}

void ViewWidget::initialize()
{
	IPlugin *plugin = FMessageWidgets->pluginManager()->pluginInterface("IMessageProcessor").value(0,NULL);
	if (plugin)
		FMessageProcessor = qobject_cast<IMessageProcessor *>(plugin->instance());
}

void ViewWidget::dropEvent(QDropEvent *AEvent)
{
	Menu *dropMenu = new Menu(this);

	bool accepted = false;
	foreach(IViewDropHandler *handler, FActiveDropHandlers)
		if (handler->viewDropAction(this, AEvent, dropMenu))
			accepted = true;

	if (accepted && !dropMenu->isEmpty())
	{
		if (dropMenu->exec(mapToGlobal(AEvent->pos())))
			AEvent->acceptProposedAction();
		else
			AEvent->ignore();
	}
	else
	{
		AEvent->ignore();
	}
	delete dropMenu;
}

void ViewWidget::dragEnterEvent(QDragEnterEvent *AEvent)
{
	FActiveDropHandlers.clear();
	foreach(IViewDropHandler *handler, FMessageWidgets->viewDropHandlers())
		if (handler->viewDragEnter(this, AEvent))
			FActiveDropHandlers.append(handler);

	if (!FActiveDropHandlers.isEmpty())
		AEvent->acceptProposedAction();
	else
		AEvent->ignore();
}

void ViewWidget::dragMoveEvent(QDragMoveEvent *AEvent)
{
	bool accepted = false;
	foreach(IViewDropHandler *handler, FActiveDropHandlers)
		if (handler->viewDragMove(this, AEvent))
			accepted = true;

	if (accepted)
		AEvent->acceptProposedAction();
	else
		AEvent->ignore();
}

void ViewWidget::dragLeaveEvent(QDragLeaveEvent *AEvent)
{
	foreach(IViewDropHandler *handler, FActiveDropHandlers)
		handler->viewDragLeave(this, AEvent);
}

void ViewWidget::onContentAppended(QWidget *AWidget, const QString &AHtml, const IMessageContentOptions &AOptions)
{
	if (AWidget == FStyleWidget)
		emit contentAppended(AHtml,AOptions);
}

void ViewWidget::onUrlClicked(QWidget *AWidget, const QUrl &AUrl)
{
	if (AWidget == FStyleWidget)
		emit urlClicked(AUrl);
}

void ViewWidget::onCustomContextMenuRequested(const QPoint &APosition)
{
	Menu *menu = new Menu(this);
	menu->setAttribute(Qt::WA_DeleteOnClose, true);

	contextMenuForView(APosition,FMessageStyle->textUnderPosition(APosition,FStyleWidget),menu);

	if (!menu->isEmpty())
		menu->popup(FStyleWidget->mapToGlobal(APosition));
	else
		delete menu;
}
