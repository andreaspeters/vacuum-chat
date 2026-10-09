#include "simplemessagestyle.h"
#include "roundedimage.h"
#include "decorationhelper.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTextCharFormat>
#include <QTextFrame>
#include <QTextCursor>
#include <QTextTable>
#include <QDomDocument>
#include <QCoreApplication>
#include <QAbstractTextDocumentLayout>
#include <QTextDocumentFragment>
#include <QUrl>
#include <definitions/menuicons.h>
#include <utils/iconstorage.h>

#define SCROLL_TIMEOUT                      100
#define SHARED_STYLE_PATH                   RESOURCES_DIR"/"RSR_STORAGE_SIMPLEMESSAGESTYLES"/"STORAGE_SHARED_DIR

static const char *SenderColors[] =  {
	"blue", "blueviolet", "brown", "cadetblue", "chocolate", "coral", "cornflowerblue", "crimson",
	"darkblue", "darkcyan", "darkgoldenrod", "darkgreen", "darkmagenta", "darkolivegreen", "darkorange",
	"darkorchid", "darkred", "darksalmon", "darkslateblue", "darkslategrey", "darkturquoise", "darkviolet",
	"deeppink", "deepskyblue", "dodgerblue", "firebrick", "forestgreen", "fuchsia", "gold", "green",
	"hotpink", "indianred", "indigo", "lightcoral", "lightseagreen", "limegreen", "magenta", "maroon",
	"mediumblue", "mediumorchid", "mediumpurple", "mediumseagreen", "mediumslateblue", "mediumvioletred",
	"midnightblue", "navy", "olive", "olivedrab", "orange", "orangered", "orchid", "palevioletred", "peru",
	"purple", "red", "rosybrown", "royalblue", "saddlebrown", "salmon", "seagreen", "sienna", "slateblue",
	"steelblue", "teal", "tomato", "violet"
};

static int SenderColorsCount = sizeof(SenderColors)/sizeof(SenderColors[0]);

namespace
{
bool isModernChatStyle(const QString &stylePath)
{
	return QFileInfo(stylePath).fileName() == QStringLiteral("modern-chat");
}

void roundModernChatImages(StyleViewer *view, const QString &html)
{
	if (!view)
		return;

	static const QRegularExpression imageSourceExpression(
		QStringLiteral("<img\\b[^>]*\\bsrc\\s*=\\s*(?:\"([^\"]*)\"|'([^']*)')"),
		QRegularExpression::CaseInsensitiveOption);
	QRegularExpressionMatchIterator matches = imageSourceExpression.globalMatch(html);
	QTextDocument *document = view->document();
	while (matches.hasNext())
	{
		const QRegularExpressionMatch match = matches.next();
		const QString source = match.captured(1).isEmpty() ? match.captured(2) : match.captured(1);
		const QUrl url = QUrl::fromEncoded(source.toUtf8());
		if (url.scheme() != QStringLiteral("vacuum-matrix-image"))
			continue;

		const QImage image = document->resource(QTextDocument::ImageResource, url).value<QImage>();
		if (!image.isNull())
			document->addResource(QTextDocument::ImageResource, url,
				RoundedImage::withRoundedCorners(image, 0.016));
	}
}
}

SimpleMessageStyle::SimpleMessageStyle(const QString &AStylePath, QNetworkAccessManager *ANetworkAccessManager, QObject *AParent) : QObject(AParent)
{
	FStylePath = AStylePath;
	FInfo = styleInfo(AStylePath);
	FVariants = styleVariants(AStylePath);
	FNetworkAccessManager=ANetworkAccessManager;

	initStyleSettings();
	loadTemplates();
	loadSenderColors();

	FScrollTimer.setSingleShot(true);
	FScrollTimer.setInterval(SCROLL_TIMEOUT);
	connect(&FScrollTimer,SIGNAL(timeout()),SLOT(onScrollAfterResize()));

	connect(AParent,SIGNAL(styleWidgetAdded(IMessageStyle *, QWidget *)),SLOT(onStyleWidgetAdded(IMessageStyle *, QWidget *)));
}

SimpleMessageStyle::~SimpleMessageStyle()
{

}

bool SimpleMessageStyle::isValid() const
{
	return !FIn_ContentHTML.isEmpty() && !styleId().isEmpty();
}

QString SimpleMessageStyle::styleId() const
{
	return FInfo.value(MSIV_NAME).toString();
}

QList<QWidget *> SimpleMessageStyle::styleWidgets() const
{
	return FWidgetStatus.keys();
}

QWidget *SimpleMessageStyle::createWidget(const IMessageStyleOptions &AOptions, QWidget *AParent)
{
	StyleViewer *view = new StyleViewer(AParent);
	view->setNetworkAccessManager(FNetworkAccessManager);
	changeOptions(view,AOptions,true);
	return view;
}

QString SimpleMessageStyle::senderColor(const QString &ASenderId) const
{
	if (!FSenderColors.isEmpty())
		return FSenderColors.at(qHash(ASenderId) % FSenderColors.count());
	return QString(SenderColors[qHash(ASenderId) % SenderColorsCount]);
}

QTextDocumentFragment SimpleMessageStyle::selection(QWidget *AWidget) const
{
	StyleViewer *view = qobject_cast<StyleViewer *>(AWidget);
	if (!view)
		return QTextDocumentFragment();
	const QTextDocumentFragment bubbleSelection = view->bubbleSelection();
	return bubbleSelection.isEmpty() ? view->textCursor().selection() : bubbleSelection;
}

QTextDocumentFragment SimpleMessageStyle::textUnderPosition(const QPoint &APosition, QWidget *AWidget) const
{
	StyleViewer *view = qobject_cast<StyleViewer *>(AWidget);
	if (view)
	{
		const QTextDocumentFragment bubbleSelection = view->bubbleSelection();
		if (!bubbleSelection.isEmpty())
			return bubbleSelection;
		const QTextDocumentFragment bubbleFragment = view->bubbleTextUnderPosition(APosition);
		if (!bubbleFragment.isEmpty())
			return bubbleFragment;

		QTextCursor cursor = view->cursorForPosition(APosition);
		if (view->textCursor().selection().isEmpty() || view->textCursor().selectionStart()>cursor.position() || view->textCursor().selectionEnd()<cursor.position())
		{
			if (!view->anchorAt(APosition).isEmpty())
			{
				QTextBlock block = cursor.block();
				for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it)
				{
					if (it.fragment().contains(cursor.position()))
					{
						cursor.setPosition(it.fragment().position());
						cursor.movePosition(QTextCursor::NextCharacter,QTextCursor::KeepAnchor,it.fragment().length());
						break;
					}
				}
			}
			else
			{
				cursor.select(QTextCursor::WordUnderCursor);
			}
			return cursor.selection();
		}
		else
		{
			return selection(AWidget);
		}
	}
	return QTextDocumentFragment();
}

bool SimpleMessageStyle::changeOptions(QWidget *AWidget, const IMessageStyleOptions &AOptions, bool AClean)
{
	StyleViewer *view = qobject_cast<StyleViewer *>(AWidget);
	if (view && AOptions.extended.value(MSO_STYLE_ID).toString()==styleId())
	{
		if (!FWidgetStatus.contains(view))
		{
			AClean = true;
			FWidgetStatus[view].scrollStarted = false;
			FWidgetStatus[view].followTail = true;
			FWidgetStatus[view].suppressTailCorrection = false;
			view->installEventFilter(this);
			connect(view->document()->documentLayout(),
				&QAbstractTextDocumentLayout::documentSizeChanged, this,
				[this, view](const QSizeF &) {
					if (!FWidgetStatus.contains(view))
						return;
					WidgetStatus &wstatus = FWidgetStatus[view];
					if (wstatus.followTail && !wstatus.suppressTailCorrection)
					{
						wstatus.scrollStarted = true;
						FScrollTimer.start();
					}
				});
			connect(view, &StyleViewer::userScrollPositionChanged, this,
				[this, view](int APosition, int AMaximum) {
					if (FWidgetStatus.contains(view))
					{
						WidgetStatus &wstatus = FWidgetStatus[view];
						wstatus.followTail = APosition >= AMaximum;
						wstatus.suppressTailCorrection = false;
						if (!wstatus.followTail)
							wstatus.scrollStarted = false;
					}
				});
			connect(view,SIGNAL(anchorClicked(const QUrl &)),SLOT(onLinkClicked(const QUrl &)));
			connect(view, &StyleViewer::bubbleAnchorClicked, this,
				[this, view](const QUrl &AUrl) { emit urlClicked(view, AUrl); });
			connect(view,SIGNAL(destroyed(QObject *)),SLOT(onStyleWidgetDestroyed(QObject *)));
			emit widgetAdded(AWidget);
		}
		else
		{
			FWidgetStatus[view].lastKind = -1;
		}

		if (AClean)
		{
			view->clearMessageBubbles();
			WidgetStatus &wstatus = FWidgetStatus[view];
			wstatus.lastKind = -1;
			wstatus.lastId = QString();
			wstatus.lastTime = QDateTime();
			wstatus.suppressTailCorrection = false;
			setVariant(AWidget, AOptions.extended.value(MSO_VARIANT).toString());
			QString html = makeStyleTemplate();
			fillStyleKeywords(html,AOptions);
			view->setHtml(html);
		}

		QFont font;
		int fontSize = AOptions.extended.value(MSO_FONT_SIZE).toInt();
		QString fontFamily = AOptions.extended.value(MSO_FONT_FAMILY).toString();
		if (fontSize>0)
			font.setPointSize(fontSize);
		if (!fontFamily.isEmpty())
			font.setFamily(fontFamily);
		view->document()->setDefaultFont(font);
		view->setAnimated(!AOptions.extended.value(MSO_ANIMATION_DISABLED).toBool());

		emit optionsChanged(AWidget,AOptions,AClean);
		return true;
	}
	return false;
}

bool SimpleMessageStyle::appendContent(QWidget *AWidget, const QString &AHtml, const IMessageContentOptions &AOptions)
{
	StyleViewer *view = FWidgetStatus.contains(AWidget) ? qobject_cast<StyleViewer *>(AWidget) : NULL;
	if (view)
	{
		const bool modernChat = isModernChatStyle(FStylePath);
		if (modernChat)
		{
			roundModernChatImages(view, AHtml);
		}
		const bool sameSender = isSameSender(AWidget,AOptions);
		QString html = makeContentTemplate(AOptions,sameSender);
		fillContentKeywords(html,AOptions,sameSender,view);
		QString contentHtml = AHtml;
		contentHtml.remove(QChar::Null);
		const QString preparedMessage = prepareMessage(contentHtml,AOptions);
		QString bubbleMarker;
		if (modernChat && AOptions.kind == IMessageContentOptions::KindMessage)
		{
			static quint64 markerSequence = 0;
			bubbleMarker = QStringLiteral("QFrameMessageMarker-%1").arg(++markerSequence);
			const QString markerHtml = DecorationHelper::bubbleMarkerHtml(bubbleMarker);
			html.replace("%message%", markerHtml + preparedMessage);
		}
		else
		{
			html.replace("%message%", preparedMessage);
		}

		WidgetStatus &wstatus = FWidgetStatus[AWidget];
		wstatus.suppressTailCorrection = AOptions.noScroll;
		const bool shouldFollowTail = wstatus.followTail && !AOptions.noScroll;

		QTextCursor cursor(view->document());
		cursor.movePosition(QTextCursor::End);
		const int insertionPosition = cursor.position();
		cursor.insertHtml(html);

		if (!bubbleMarker.isEmpty())
		{
			QTextCursor searchCursor(view->document());
			searchCursor.setPosition(insertionPosition);
			QTextCursor markerCursor = view->document()->find(bubbleMarker, searchCursor);
			if (!markerCursor.isNull())
			{
				QTextTable *bubbleTable = markerCursor.currentTable();
				markerCursor.removeSelectedText();
				QRectF sourceAnchorRect;
				QTextCursor sourceTextCursor;
				if (bubbleTable)
				{
					const QTextTableCell messageCell = bubbleTable->cellAt(markerCursor);
					if (messageCell.isValid())
					{
						view->document()->documentLayout()->documentSize();
						sourceAnchorRect = view->document()->documentLayout()->frameBoundingRect(bubbleTable);
						sourceTextCursor = messageCell.firstCursorPosition();
						sourceTextCursor.setPosition(messageCell.lastCursorPosition().position(),
							QTextCursor::KeepAnchor);
					}
				}
				QColor fill(AOptions.textBGColor);
				if (!fill.isValid())
					fill = AOptions.direction == IMessageContentOptions::DirectionOut
						? QColor(QStringLiteral("#e8f1ff")) : QColor(QStringLiteral("#f1f2f4"));
				view->addMessageBubble(bubbleTable, AOptions.messageId, preparedMessage, fill,
					AOptions.direction == IMessageContentOptions::DirectionOut,
					sourceAnchorRect, sourceTextCursor);
			}
		}

		if (shouldFollowTail)
		{
			view->verticalScrollBar()->setSliderPosition(view->verticalScrollBar()->maximum());
			wstatus.scrollStarted = true;
			FScrollTimer.start();
		}
		else if (AOptions.noScroll)
		{
			wstatus.scrollStarted = false;
		}

		wstatus.lastKind = AOptions.kind;
		wstatus.lastId = AOptions.senderId;
		wstatus.lastTime = AOptions.time;

		emit contentAppended(AWidget,AHtml,AOptions);
		return true;
	}
	return false;
}

bool SimpleMessageStyle::setMessageDecoration(QWidget *AWidget, const QString &AMessageId,
	const QString &ADecorationId, const QString &AHtml)
{
	if (!FWidgetStatus.contains(AWidget))
		return false;
	StyleViewer *view = qobject_cast<StyleViewer *>(AWidget);
	return view && view->setMessageDecoration(AMessageId, ADecorationId, AHtml);
}

bool SimpleMessageStyle::replaceMessageContent(QWidget *AWidget, const QString &AMessageId,
	const QString &AHtml)
{
	if (!FWidgetStatus.contains(AWidget) || AMessageId.isEmpty())
		return false;
	StyleViewer *view = qobject_cast<StyleViewer *>(AWidget);
	if (!view)
		return false;
	if (isModernChatStyle(FStylePath))
		roundModernChatImages(view, AHtml);
	return view->replaceMessageBubbleContent(AMessageId, AHtml);
}

QMap<QString, QVariant> SimpleMessageStyle::infoValues() const
{
	return FInfo;
}

QList<QString> SimpleMessageStyle::variants() const
{
	return FVariants;
}

QList<QString> SimpleMessageStyle::styleVariants(const QString &AStylePath)
{
	QList<QString> files;
	if (!AStylePath.isEmpty())
	{
		QDir dir(AStylePath+"/Variants");
		files = dir.entryList(QStringList("*.css"),QDir::Files,QDir::Name);
		for (int i=0; i<files.count();i++)
			files[i].chop(4);
	}
	return files;
}

QMap<QString, QVariant> SimpleMessageStyle::styleInfo(const QString &AStylePath)
{
	QMap<QString, QVariant> info;

	QFile file(AStylePath+"/Info.plist");
	if (!AStylePath.isEmpty() && file.open(QFile::ReadOnly))
	{
		QDomDocument doc;
		if (doc.setContent(file.readAll(),true))
		{
			QDomElement elem = doc.documentElement().firstChildElement("dict").firstChildElement("key");
			while (!elem.isNull())
			{
				QString key = elem.text();
				if (!key.isEmpty())
				{
					elem = elem.nextSiblingElement();
					if (elem.tagName() == "string")
						info.insert(key,elem.text());
					else if (elem.tagName() == "integer")
						info.insert(key,elem.text().toInt());
					else if (elem.tagName() == "true")
						info.insert(key,true);
					else if (elem.tagName() == "false")
						info.insert(key,false);
				}
				elem = elem.nextSiblingElement("key");
			}
		}
	}
	return info;
}

bool SimpleMessageStyle::isSameSender(QWidget *AWidget, const IMessageContentOptions &AOptions) const
{
	if (!FCombineConsecutive)
		return false;
	if (AOptions.kind != IMessageContentOptions::KindMessage)
		return false;
	if (AOptions.senderId.isEmpty())
		return false;

	const WidgetStatus &wstatus = FWidgetStatus.value(AWidget);
	if (wstatus.lastKind != AOptions.kind)
		return false;
	if (wstatus.lastId != AOptions.senderId)
		return false;
	if (wstatus.lastTime.secsTo(AOptions.time)>2*60)
		return false;

	return true;
}

void SimpleMessageStyle::setVariant(QWidget *AWidget, const QString &AVariant)
{
	StyleViewer *view = FWidgetStatus.contains(AWidget) ? qobject_cast<StyleViewer *>(AWidget) : NULL;
	if (view)
	{
		QString variant = QString("Variants/%1.css").arg(!FVariants.contains(AVariant) ? FInfo.value(MSIV_DEFAULT_VARIANT,"main").toString() : AVariant);
		view->document()->setDefaultStyleSheet(loadFileData(FStylePath+"/"+variant,QString()));
	}
}

QString SimpleMessageStyle::makeStyleTemplate() const
{
	QString htmlFileName = FStylePath+"/Template.html";
	if (!QFile::exists(htmlFileName))
		htmlFileName = qApp->applicationDirPath()+"/"SHARED_STYLE_PATH"/Template.html";

	return loadFileData(htmlFileName,QString());
}

void SimpleMessageStyle::fillStyleKeywords(QString &AHtml, const IMessageStyleOptions &AOptions) const
{
	QString background;
	if (FAllowCustomBackground)
	{
		if (!AOptions.extended.value(MSO_BG_IMAGE_FILE).toString().isEmpty())
		{
			background.append("background-image: url('%1'); ");
			background = background.arg(QUrl::fromLocalFile(AOptions.extended.value(MSO_BG_IMAGE_FILE).toString()).toString());
		}
		if (!AOptions.extended.value(MSO_BG_COLOR).toString().isEmpty())
		{
			background.append(QString("background-color: %1; ").arg(AOptions.extended.value(MSO_BG_COLOR).toString()));
		}
	}
	AHtml.replace("%bodyBackground%", background);
}

QString SimpleMessageStyle::makeContentTemplate(const IMessageContentOptions &AOptions, bool ASameSender) const
{
	QString html;
	if (AOptions.kind==IMessageContentOptions::KindTopic && !FTopicHTML.isEmpty())
	{
		html = FTopicHTML;
	}
	else if (AOptions.kind==IMessageContentOptions::KindStatus && !FStatusHTML.isEmpty())
	{
		html = FStatusHTML;
	}
	else if (AOptions.kind==IMessageContentOptions::KindMeCommand && (!FMeCommandHTML.isEmpty() || !FStatusHTML.isEmpty()))
	{
		html = !FMeCommandHTML.isEmpty() ? FMeCommandHTML : FStatusHTML;
	}
	else
	{
		if (AOptions.direction == IMessageContentOptions::DirectionIn)
		{
			html = ASameSender ? FIn_NextContentHTML : FIn_ContentHTML;
		}
		else
		{
			html = ASameSender ? FOut_NextContentHTML : FOut_ContentHTML;
		}
	}
	return html;
}

bool SimpleMessageStyle::isAvatarExistsCached(const QString &APath) const
{
	return FAvatarExistsCache.exists(APath, [](const QString &path) {
		return QFile::exists(path);
	});
}

void SimpleMessageStyle::fillContentKeywords(QString &AHtml, const IMessageContentOptions &AOptions,
	bool ASameSender, StyleViewer *AView) const
{
	bool isDirectionIn = AOptions.direction == IMessageContentOptions::DirectionIn;

	QStringList messageClasses;
	if (FCombineConsecutive && ASameSender)
		messageClasses << MSMC_CONSECUTIVE;

	if (AOptions.kind==IMessageContentOptions::KindMeCommand)
		messageClasses << (!FMeCommandHTML.isEmpty() ? MSMC_MECOMMAND : MSMC_STATUS);
	else if (AOptions.kind == IMessageContentOptions::KindStatus)
		messageClasses << MSMC_STATUS;
	else
		messageClasses << MSMC_MESSAGE;
	
	if (isDirectionIn)
		messageClasses << MSMC_INCOMING;
	else
		messageClasses << MSMC_OUTGOING;
	
	if (AOptions.type & IMessageContentOptions::TypeGroupchat)
		messageClasses << MSMC_GROUPCHAT;
	if (AOptions.type & IMessageContentOptions::TypeHistory)
		messageClasses << MSMC_HISTORY;
	if (AOptions.type & IMessageContentOptions::TypeEvent)
		messageClasses << MSMC_EVENT;
	if (AOptions.type & IMessageContentOptions::TypeMention)
		messageClasses << MSMC_MENTION;
	if (AOptions.type & IMessageContentOptions::TypeNotification)
		messageClasses << MSMC_NOTIFICATION;

	QString messageStatus;
	if (AOptions.status == IMessageContentOptions::StatusOnline)
		messageStatus = MSSK_ONLINE;
	else if (AOptions.status == IMessageContentOptions::StatusOffline)
		messageStatus = MSSK_OFFLINE;
	else if (AOptions.status == IMessageContentOptions::StatusAway)
		messageStatus = MSSK_AWAY;
	else if (AOptions.status == IMessageContentOptions::StatusAwayMessage)
		messageStatus = MSSK_AWAY_MESSAGE;
	else if (AOptions.status == IMessageContentOptions::StatusReturnAway)
		messageStatus = MSSK_RETURN_AWAY;
	else if (AOptions.status == IMessageContentOptions::StatusIdle)
		messageStatus = MSSK_IDLE;
	else if (AOptions.status == IMessageContentOptions::StatusReturnIdle)
		messageStatus = MSSK_RETURN_IDLE;
	else if (AOptions.status == IMessageContentOptions::StatusDateSeparator)
		messageStatus = MSSK_DATE_SEPARATOR;
	else if (AOptions.status == IMessageContentOptions::StatusJoined)
		messageStatus = MSSK_CONTACT_JOINED;
	else if (AOptions.status == IMessageContentOptions::StatusLeft)
		messageStatus = MSSK_CONTACT_LEFT;
	else if (AOptions.status == IMessageContentOptions::StatusError)
		messageStatus = MSSK_ERROR;
	else if (AOptions.status == IMessageContentOptions::StatusTimeout)
		messageStatus = MSSK_TIMED_OUT;
	else if (AOptions.status == IMessageContentOptions::StatusEncryption)
		messageStatus = MSSK_ENCRYPTION;
	else if (AOptions.status == IMessageContentOptions::StatusFileTransferBegan)
		messageStatus = MSSK_FILETRANSFER_BEGAN;
	else if (AOptions.status == IMessageContentOptions::StatusFileTransferComplete)
		messageStatus = MSSK_FILETRANSFER_COMPLETE;
	if (!messageStatus.isEmpty())
		messageClasses << messageStatus;

	AHtml.replace("%messageClasses%", messageClasses.join(" "));

	AHtml.replace("%senderStatusIcon%",AOptions.senderIcon);
	const QDateTime displayTime = ASameSender && AOptions.kind == IMessageContentOptions::KindMessage
		? QDateTime() : AOptions.time;
	const QString shortTimeFormat = AOptions.timeFormat.isEmpty()
		? tr("hh:mm") : AOptions.timeFormat;
	AHtml.replace("%shortTime%", displayTime.toString(shortTimeFormat).toHtmlEscaped());

	QString avatar = AOptions.senderAvatar;
	if (!isAvatarExistsCached(avatar))
	{
		const QString emptyAvatar = IconStorage::staticStorage(RSR_STORAGE_MENUICONS)->fileFullName(MNI_AVATAR_EMPTY);
		avatar = isAvatarExistsCached(emptyAvatar) ? emptyAvatar :
			FStylePath+(isDirectionIn ? "/Incoming/buddy_icon.png" : "/Outgoing/buddy_icon.png");
		if (!isDirectionIn && !isAvatarExistsCached(avatar))
			avatar = FStylePath+"/Incoming/buddy_icon.png";
		if (!isAvatarExistsCached(avatar))
			avatar = qApp->applicationDirPath()+"/"SHARED_STYLE_PATH"/buddy_icon.png";
	}
	AHtml.replace("%userIconPath%", AView->cacheImageResource(avatar, true).toHtmlEscaped());

	QString timeFormat = !AOptions.timeFormat.isEmpty() ? AOptions.timeFormat : tr("hh:mm:ss");
	QString time = displayTime.toString(timeFormat).toHtmlEscaped();
	AHtml.replace("%time%", time);

	QString sColor = !AOptions.senderColor.isEmpty() ? AOptions.senderColor : senderColor(AOptions.senderId);
	AHtml.replace("%senderColor%",sColor);

	AHtml.replace("%sender%",AOptions.senderName);
	AHtml.replace("%senderScreenName%",AOptions.senderId);
	AHtml.replace("%textbackgroundcolor%",!AOptions.textBGColor.isEmpty() ? AOptions.textBGColor : "inherit");
}

QString SimpleMessageStyle::prepareMessage(const QString &AHtml, const IMessageContentOptions &AOptions) const
{
	if (AOptions.kind==IMessageContentOptions::KindMeCommand && FMeCommandHTML.isEmpty())
	{
		QTextDocument doc;
		doc.setHtml(AHtml);
		QTextCursor cursor(&doc);
		cursor.insertHtml(QString("<i>*&nbsp;%1</i>&nbsp;").arg(AOptions.senderName));
		return TextManager::getDocumentBody(doc);
	}
	return AHtml;
}

QString SimpleMessageStyle::loadFileData(const QString &AFileName, const QString &DefValue) const
{
	if (QFile::exists(AFileName))
	{
		QFile file(AFileName);
		if (file.open(QFile::ReadOnly))
		{
			QByteArray html = file.readAll();
			return QString::fromUtf8(html.data(),html.size());
		}
	}
	return DefValue;
}

void SimpleMessageStyle::loadTemplates()
{
	FIn_ContentHTML =      loadFileData(FStylePath+"/Incoming/Content.html",QString());
	FIn_NextContentHTML =  loadFileData(FStylePath+"/Incoming/NextContent.html",FIn_ContentHTML);

	FOut_ContentHTML =     loadFileData(FStylePath+"/Outgoing/Content.html",FIn_ContentHTML);
	FOut_NextContentHTML = loadFileData(FStylePath+"/Outgoing/NextContent.html",FOut_ContentHTML);

	FTopicHTML =           loadFileData(FStylePath+"/Topic.html",QString());
	FStatusHTML =          loadFileData(FStylePath+"/Status.html",FIn_ContentHTML);
	FMeCommandHTML =       loadFileData(FStylePath+"/MeCommand.html",QString());
}

void SimpleMessageStyle::loadSenderColors()
{
	QFile colors(FStylePath+"/Incoming/SenderColors.txt");
	if (colors.open(QFile::ReadOnly))
		FSenderColors = QString::fromUtf8(colors.readAll()).split(':',Qt::SkipEmptyParts);
}

void SimpleMessageStyle::initStyleSettings()
{
	FCombineConsecutive = !FInfo.value(MSIV_DISABLE_COMBINE_CONSECUTIVE,false).toBool();
	FAllowCustomBackground = !FInfo.value(MSIV_DISABLE_CUSTOM_BACKGROUND,false).toBool();
}

bool SimpleMessageStyle::eventFilter(QObject *AWatched, QEvent *AEvent)
{
	if (AEvent->type() == QEvent::Resize)
	{
		StyleViewer *view = qobject_cast<StyleViewer *>(AWatched);
		if (FWidgetStatus.contains(view))
		{
			WidgetStatus &wstatus = FWidgetStatus[view];
			if (!wstatus.scrollStarted && wstatus.followTail && !wstatus.suppressTailCorrection)
			{
				wstatus.scrollStarted = true;
				FScrollTimer.start();
			}
		}
	}
	return QObject::eventFilter(AWatched,AEvent);
}

void SimpleMessageStyle::onLinkClicked(const QUrl &AUrl)
{
	StyleViewer *view = qobject_cast<StyleViewer *>(sender());
	emit urlClicked(view,AUrl);
}

void SimpleMessageStyle::onScrollAfterResize()
{
	for (QMap<QWidget*,WidgetStatus>::iterator it = FWidgetStatus.begin(); it!= FWidgetStatus.end(); ++it)
	{
		if (it->scrollStarted)
		{
			it->scrollStarted = false;
			if (it->followTail && !it->suppressTailCorrection)
			{
				StyleViewer *view = qobject_cast<StyleViewer *>(it.key());
				QScrollBar *scrollBar = view->verticalScrollBar();
				scrollBar->setSliderPosition(scrollBar->maximum());
			}
		}
	}
}

void SimpleMessageStyle::onStyleWidgetAdded(IMessageStyle *AStyle, QWidget *AWidget)
{
	if (AStyle!=this && FWidgetStatus.contains(AWidget))
	{
		AWidget->removeEventFilter(this);
		FWidgetStatus.remove(AWidget);
		emit widgetRemoved(AWidget);
	}
}

void SimpleMessageStyle::onStyleWidgetDestroyed(QObject *AObject)
{
	FWidgetStatus.remove((QWidget *)AObject);
	emit widgetRemoved((QWidget *)AObject);
}
