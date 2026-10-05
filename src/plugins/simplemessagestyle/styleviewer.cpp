#include "styleviewer.h"
#include "roundedimage.h"

#include <QAbstractSlider>
#include <QAbstractTextDocumentLayout>
#include <QCryptographicHash>
#include <QFrame>
#include <QImage>
#include <QKeyEvent>
#include <QPalette>
#include <QPointer>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextFragment>
#include <QTextTable>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <utils/imageloadscheduler.h>

StyleViewer::StyleViewer(QWidget *AParent) : AnimatedTextBrowser(AParent)
{
	setOpenLinks(false);
	setAcceptDrops(false);
	setOpenExternalLinks(false);
	setFrameShape(QFrame::NoFrame);
	setContextMenuPolicy(Qt::CustomContextMenu);
	connect(verticalScrollBar(), &QScrollBar::sliderMoved, this,
		[this](int APosition) {
			emit userScrollPositionChanged(APosition, verticalScrollBar()->maximum());
		});
	connect(verticalScrollBar(), &QAbstractSlider::actionTriggered, this,
		[this](int) {
			QTimer::singleShot(0, this, [this]() {
				notifyUserScrollPositionChanged();
			});
		});
	connect(verticalScrollBar(), &QScrollBar::valueChanged,
		this, &StyleViewer::updateMessageBubbleGeometry);
	connect(horizontalScrollBar(), &QScrollBar::valueChanged,
		this, &StyleViewer::updateMessageBubbleGeometry);
	connect(document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged,
		this, [this](const QSizeF &) { updateMessageBubbleGeometry(); });
}

StyleViewer::~StyleViewer()
{

}

QString StyleViewer::cacheImageResource(const QString &APath, bool ARoundCorners)
{
	if (APath.isEmpty())
		return QString();

	const QString key = ImageLoadScheduler::fileKey(APath);
	QUrl resourceUrl;
	resourceUrl.setScheme(QStringLiteral("vacuum-avatar"));
	resourceUrl.setPath(QString::fromLatin1(QCryptographicHash::hash(
		key.toUtf8(), QCryptographicHash::Sha256).toHex()));
	QTextDocument *textDocument = document();
	if (!textDocument->resource(QTextDocument::ImageResource, resourceUrl).isValid())
	{
		QImage placeholder(1, 1, QImage::Format_ARGB32_Premultiplied);
		placeholder.fill(Qt::transparent);
		textDocument->addResource(QTextDocument::ImageResource, resourceUrl, placeholder);

		if (!FPendingImageResources.contains(key))
		{
			if (ImageLoadScheduler *scheduler = ImageLoadScheduler::instance())
			{
				FPendingImageResources.insert(key);
				QPointer<StyleViewer> viewer(this);
				scheduler->loadFile(APath, this, [viewer, key, resourceUrl, ARoundCorners](const QImage &image) {
					if (!viewer)
						return;
					viewer->FPendingImageResources.remove(key);
					if (image.isNull())
						return;
					QTextDocument *doc = viewer->document();
					const QImage displayImage = ARoundCorners
						? RoundedImage::withRoundedCorners(image, 0.16, QColor(QStringLiteral("#e1e5ea")))
						: image;
					doc->addResource(QTextDocument::ImageResource, resourceUrl, displayImage);
					doc->markContentsDirty(0, doc->characterCount());
					viewer->viewport()->update();
				});
			}
		}
	}
	return resourceUrl.toString(QUrl::FullyEncoded);
}

void StyleViewer::addMessageBubble(QTextTable *ATable, const QString &AHtml, const QColor &AFill)
{
	if (!ATable)
		return;

	QFrame *frame = new QFrame(viewport());
	frame->setObjectName(QStringLiteral("modernChatBubbleFrame"));
	frame->setFrameShape(QFrame::NoFrame);
	frame->setAttribute(Qt::WA_StyledBackground, true);
	frame->setStyleSheet(QStringLiteral(
		"QFrame#modernChatBubbleFrame { border: 1px solid transparent; border-radius: 6px; background-clip: padding; background-color: %1; }")
		.arg(AFill.name(QColor::HexRgb)));

	QVBoxLayout *layout = new QVBoxLayout(frame);
	layout->setContentsMargins(6, 6, 6, 6);
	layout->setSpacing(0);

	QTextBrowser *content = new QTextBrowser(frame);
	content->setObjectName(QStringLiteral("modernChatBubbleContent"));
	content->setFrameShape(QFrame::NoFrame);
	content->setReadOnly(true);
	content->setOpenLinks(false);
	content->setOpenExternalLinks(false);
	content->setContextMenuPolicy(Qt::NoContextMenu);
	content->setTextInteractionFlags(Qt::TextBrowserInteraction);
	content->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	content->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	content->setLineWrapMode(QTextEdit::WidgetWidth);
	content->setStyleSheet(QStringLiteral(
		"QTextBrowser#modernChatBubbleContent { background: transparent; border: none; }") +
		QStringLiteral("QTextBrowser#modernChatBubbleContent viewport { background: transparent; }"));
	content->setAttribute(Qt::WA_TranslucentBackground, true);
	content->viewport()->setAutoFillBackground(false);
	QPalette palette = content->palette();
	palette.setColor(QPalette::Base, Qt::transparent);
	content->setPalette(palette);
	content->document()->setDocumentMargin(0);
	content->document()->setDefaultFont(document()->defaultFont());
	content->document()->setDefaultStyleSheet(document()->defaultStyleSheet() +
		QStringLiteral("\nbody { margin: 0; padding: 0; background: transparent; }\n") +
		QStringLiteral(".xxxmessage { background-color: transparent; }"));

	static const QRegularExpression imageSourceExpression(
		QStringLiteral("<img\\b[^>]*\\bsrc\\s*=\\s*(?:\"([^\"]*)\"|'([^']*)')"),
		QRegularExpression::CaseInsensitiveOption);
	QRegularExpressionMatchIterator matches = imageSourceExpression.globalMatch(AHtml);
	QTextDocument *contentDocument = content->document();
	while (matches.hasNext())
	{
		const QRegularExpressionMatch match = matches.next();
		const QString source = match.captured(1).isEmpty() ? match.captured(2) : match.captured(1);
		const QUrl url = QUrl::fromEncoded(source.toUtf8());
		const QVariant image = document()->resource(QTextDocument::ImageResource, url);
		if (image.isValid())
			contentDocument->addResource(QTextDocument::ImageResource, url, image);
	}
	connect(this, &AnimatedTextBrowser::resourceUpdated, content,
		[this, content](const QUrl &AUrl) {
			const QVariant image = document()->resource(QTextDocument::ImageResource, AUrl);
			if (image.isValid())
			{
				QTextDocument *doc = content->document();
				doc->addResource(QTextDocument::ImageResource, AUrl, image);
				doc->markContentsDirty(0, doc->characterCount());
			}
		});
	connect(content, &QTextBrowser::anchorClicked, this, &StyleViewer::bubbleAnchorClicked);

	content->setHtml(QStringLiteral(
		"<div class=\"xxxmessage\" style=\"background-color:transparent;\">%1</div>").arg(AHtml));
	layout->addWidget(content);
	FBubbleOverlays.append({ATable, frame, content});
	frame->show();
	updateMessageBubbleGeometry();
	QTimer::singleShot(0, this, &StyleViewer::updateMessageBubbleGeometry);
}

void StyleViewer::clearMessageBubbles()
{
	for (const BubbleOverlay &overlay : FBubbleOverlays)
	{
		if (overlay.frame)
			delete overlay.frame.data();
	}
	FBubbleOverlays.clear();
}

QTextDocumentFragment StyleViewer::bubbleSelection() const
{
	for (auto it = FBubbleOverlays.crbegin(); it != FBubbleOverlays.crend(); ++it)
	{
		if (it->content)
		{
			const QTextCursor cursor = it->content->textCursor();
			if (cursor.hasSelection())
				return QTextDocumentFragment(cursor);
		}
	}
	return QTextDocumentFragment();
}

QTextDocumentFragment StyleViewer::bubbleTextUnderPosition(const QPoint &APosition) const
{
	const QPoint viewportPosition = viewport()->mapFrom(this, APosition);
	for (auto it = FBubbleOverlays.crbegin(); it != FBubbleOverlays.crend(); ++it)
	{
		if (!it->frame || !it->content || !it->frame->geometry().contains(viewportPosition))
			continue;

		const QPoint contentPosition = it->content->viewport()->mapFrom(it->frame, viewportPosition);
		QTextCursor cursor = it->content->cursorForPosition(contentPosition);
		const QString anchor = it->content->anchorAt(contentPosition);
		if (!anchor.isEmpty())
		{
			const QTextBlock block = cursor.block();
			for (QTextBlock::iterator fragmentIt = block.begin(); !fragmentIt.atEnd(); ++fragmentIt)
			{
				const QTextFragment fragment = fragmentIt.fragment();
				if (fragment.isValid() && fragment.charFormat().isAnchor() &&
					cursor.position() >= fragment.position() &&
					cursor.position() <= fragment.position() + fragment.length())
				{
					cursor.setPosition(fragment.position());
					cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor,
						fragment.length());
					break;
				}
			}
		}
		else
		{
			cursor.select(QTextCursor::WordUnderCursor);
		}
		return QTextDocumentFragment(cursor);
	}
	return QTextDocumentFragment();
}

void StyleViewer::updateMessageBubbleGeometry()
{
	if (!document() || !document()->documentLayout())
		return;
	for (const BubbleOverlay &overlay : FBubbleOverlays)
	{
		if (!overlay.table || !overlay.frame)
			continue;
		QRectF rect = document()->documentLayout()->frameBoundingRect(overlay.table);
		rect.translate(-horizontalScrollBar()->value(), -verticalScrollBar()->value());
		overlay.frame->setGeometry(rect.toAlignedRect());
		overlay.frame->raise();
	}
}

void StyleViewer::wheelEvent(QWheelEvent *AEvent)
{
	AnimatedTextBrowser::wheelEvent(AEvent);
	notifyUserScrollPositionChanged();
}

void StyleViewer::keyPressEvent(QKeyEvent *AEvent)
{
	const int key = AEvent->key();
	const bool scrollKey = key == Qt::Key_Up || key == Qt::Key_Down ||
		key == Qt::Key_PageUp || key == Qt::Key_PageDown ||
		key == Qt::Key_Home || key == Qt::Key_End || key == Qt::Key_Space;
	AnimatedTextBrowser::keyPressEvent(AEvent);
	if (scrollKey)
		notifyUserScrollPositionChanged();
}

void StyleViewer::resizeEvent(QResizeEvent *AEvent)
{
	AnimatedTextBrowser::resizeEvent(AEvent);
	updateMessageBubbleGeometry();
}

void StyleViewer::notifyUserScrollPositionChanged()
{
	QScrollBar *scrollBar = verticalScrollBar();
	emit userScrollPositionChanged(scrollBar->sliderPosition(), scrollBar->maximum());
}
