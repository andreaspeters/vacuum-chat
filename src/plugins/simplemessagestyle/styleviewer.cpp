#include "styleviewer.h"

#include <QAbstractSlider>
#include <QAbstractTextDocumentLayout>
#include <QCryptographicHash>
#include <QFrame>
#include <QImage>
#include <QKeyEvent>
#include <QPixmap>
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
#include <utils/roundedavatar.h>

namespace
{
QSize imageSizeForResource(const QVariant &resource)
{
	const QImage image = resource.value<QImage>();
	if (!image.isNull())
		return image.size();

	const QPixmap pixmap = resource.value<QPixmap>();
	if (!pixmap.isNull())
		return pixmap.size();

	QImage decoded;
	const QByteArray data = resource.toByteArray();
	if (!data.isEmpty() && decoded.loadFromData(data))
		return decoded.size();
	return QSize();
}
}

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
		this, &StyleViewer::scheduleMessageBubbleGeometryUpdate);
	connect(horizontalScrollBar(), &QScrollBar::valueChanged,
		this, &StyleViewer::scheduleMessageBubbleGeometryUpdate);
	connect(document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged,
		this, [this](const QSizeF &) { scheduleMessageBubbleGeometryUpdate(); });
	connect(this, &AnimatedTextBrowser::resourceUpdated,
		this, &StyleViewer::updateMessageBubbleResource);
	connect(this, &AnimatedTextBrowser::resourceLoaded,
		this, &StyleViewer::updateMessageBubbleResource, Qt::QueuedConnection);
	FGeometryUpdateTimer.setSingleShot(true);
	FGeometryUpdateTimer.setInterval(0);
	connect(&FGeometryUpdateTimer, &QTimer::timeout,
		this, &StyleViewer::updateMessageBubbleGeometry);
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
						? RoundedAvatar::roundImageScaled(image, QSize(32, 32))
						: image;
					doc->addResource(QTextDocument::ImageResource, resourceUrl, displayImage);
					doc->markContentsDirty(0, doc->characterCount());
					viewer->viewport()->update();
					emit viewer->resourceUpdated(resourceUrl);
				});
			}
		}
	}
	return resourceUrl.toString(QUrl::FullyEncoded);
}

void StyleViewer::addMessageBubble(QTextTable *ATable, const QString &AHtml, const QColor &AFill)
{
	addMessageBubble(ATable, QString(), AHtml, AFill);
}

void StyleViewer::addMessageBubble(QTextTable *ATable, const QString &AMessageId,
	const QString &AHtml, const QColor &AFill, bool AOutgoing,
	const QRectF &ASourceAnchorRect, const QTextCursor &ASourceSpacerCursor)
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
	layout->setContentsMargins(5, 5, 0, 5);
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
	// Ensure images rendered as inline elements have a valid font size,
	// preventing "QFont::setPixelSize: Pixel size <= 0 (0)" warnings.
	QFont bubbleFont = content->document()->defaultFont();
	if (bubbleFont.pixelSize() <= 0 && bubbleFont.pointSizeF() <= 0) {
		bubbleFont.setPointSize(10);
	}
	content->document()->setDefaultFont(bubbleFont);
	content->document()->setDefaultStyleSheet(document()->defaultStyleSheet() +
		QStringLiteral("\nbody { margin: 0; padding: 0; background: transparent; }\n") +
		QStringLiteral(".xxxmessage { background-color: transparent; }\n") +
		QStringLiteral(".xxxmessage p, .xxxmessage pre, .xxxmessage blockquote { ") +
		QStringLiteral("margin-top: 0; margin-bottom: 0; padding-top: 0; padding-bottom: 0; }"));

	QTextDocument *contentDocument = content->document();
	const QSet<QUrl> bubbleImageResources = DecorationHelper::imageResourcesFromHtml(AHtml);
	for (const QUrl &url : bubbleImageResources)
	{
		const QVariant image = document()->resource(QTextDocument::ImageResource, url);
		if (image.isValid())
			contentDocument->addResource(QTextDocument::ImageResource, url, image);
	}
	connect(content, &QTextBrowser::anchorClicked, this, &StyleViewer::bubbleAnchorClicked);

	content->setHtml(QStringLiteral(
		"<div class=\"xxxmessage\" style=\"background-color:transparent;\">%1</div>").arg(AHtml));
	for (const QUrl &url : bubbleImageResources)
	{
		const QSize imageSize = imageSizeForResource(document()->resource(QTextDocument::ImageResource, url));
		if (!imageSize.isEmpty())
			DecorationHelper::updateImageFormatForResource(*contentDocument, url,
				imageSize, content->viewport()->width());
	}
	layout->addWidget(content);
	BubbleOverlay overlay;
	overlay.table = ATable;
	overlay.frame = frame;
	overlay.content = content;
	overlay.messageId = AMessageId;
	overlay.html = AHtml;
	overlay.renderedHtml = AHtml;
	overlay.sourceAnchorRect = ASourceAnchorRect;
	overlay.sourceSpacerCursor = ASourceSpacerCursor;
	overlay.outgoing = AOutgoing;
	overlay.imageResources = bubbleImageResources;
	FBubbleOverlays.append(overlay);
	scheduleMessageBubbleGeometryUpdate();
}

bool StyleViewer::setMessageDecoration(const QString &AMessageId, const QString &ADecorationId,
	const QString &AHtml)
{
	if (AMessageId.isEmpty() || ADecorationId.isEmpty())
		return false;

	bool updated = false;
	for (BubbleOverlay &overlay : FBubbleOverlays)
	{
		if (overlay.messageId != AMessageId || !overlay.content)
			continue;

		DecorationHelper::createOrUpdateDecoration(overlay.decorations, ADecorationId, AHtml, 0, 0);
		const QString renderedHtml = DecorationHelper::renderBubbleHtml(overlay.html, overlay.decorations);
		const QString adjacentHtml = DecorationHelper::renderAdjacentHtml(overlay.decorations);
		const QString reactionHtml = DecorationHelper::renderReactionHtml(overlay.decorations);
		const bool bubbleChanged = overlay.renderedHtml != renderedHtml;
		const bool reactionsChanged = overlay.renderedReactionHtml != reactionHtml;
		if (bubbleChanged)
		{
			overlay.content->setHtml(QStringLiteral(
				"<div class=\"xxxmessage\" style=\"background-color:transparent;\">%1</div>").arg(renderedHtml));
			overlay.renderedHtml = renderedHtml;
		}

		if (!adjacentHtml.isEmpty())
		{
			if (!overlay.sideContent)
			{
				QTextBrowser *sideContent = new QTextBrowser(viewport());
				sideContent->setObjectName(QStringLiteral("modernChatMessageStatus"));
				sideContent->setFrameShape(QFrame::NoFrame);
				sideContent->setReadOnly(true);
				sideContent->setOpenLinks(false);
				sideContent->setOpenExternalLinks(false);
				sideContent->setContextMenuPolicy(Qt::NoContextMenu);
				sideContent->setTextInteractionFlags(Qt::NoTextInteraction);
				sideContent->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
				sideContent->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
				sideContent->setLineWrapMode(QTextEdit::NoWrap);
				sideContent->setStyleSheet(QStringLiteral(
					"QTextBrowser#modernChatMessageStatus { background: transparent; border: none; }") +
					QStringLiteral("QTextBrowser#modernChatMessageStatus viewport { background: transparent; }"));
				sideContent->setAttribute(Qt::WA_TranslucentBackground, true);
				sideContent->viewport()->setAutoFillBackground(false);
				QPalette palette = sideContent->palette();
				palette.setColor(QPalette::Base, Qt::transparent);
				sideContent->setPalette(palette);
				sideContent->document()->setDocumentMargin(0);
				sideContent->document()->setDefaultFont(overlay.content->document()->defaultFont());
				sideContent->document()->setDefaultStyleSheet(document()->defaultStyleSheet() +
					QStringLiteral("\nbody { margin: 0; padding: 0; background: transparent; }\n"));
				overlay.sideContent = sideContent;
			}
			overlay.sideContent->setHtml(adjacentHtml);
			overlay.sideContent->document()->adjustSize();
			const QSize statusSize = overlay.sideContent->document()->size().toSize();
			overlay.sideContent->setFixedSize(qMax(12, statusSize.width() + 2),
				qMax(12, statusSize.height() + 2));
			overlay.sideContent->show();
		}
		else if (overlay.sideContent)
		{
			overlay.sideContent->clear();
			overlay.sideContent->hide();
		}

		if (!reactionHtml.isEmpty())
		{
			if (!overlay.reactionContent)
			{
				QTextBrowser *reactionContent = new QTextBrowser(viewport());
				reactionContent->setObjectName(QStringLiteral("modernChatMessageReactions"));
				reactionContent->setFrameShape(QFrame::NoFrame);
				reactionContent->setReadOnly(true);
				reactionContent->setOpenLinks(false);
				reactionContent->setOpenExternalLinks(false);
				reactionContent->setContextMenuPolicy(Qt::NoContextMenu);
				reactionContent->setTextInteractionFlags(Qt::NoTextInteraction);
				reactionContent->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
				reactionContent->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
				reactionContent->setLineWrapMode(QTextEdit::NoWrap);
				reactionContent->setStyleSheet(QStringLiteral(
					"QTextBrowser#modernChatMessageReactions { background: transparent; border: none; }") +
					QStringLiteral("QTextBrowser#modernChatMessageReactions viewport { background: transparent; }"));
				reactionContent->setAttribute(Qt::WA_TranslucentBackground, true);
				reactionContent->viewport()->setAutoFillBackground(false);
				QPalette palette = reactionContent->palette();
				palette.setColor(QPalette::Base, Qt::transparent);
				reactionContent->setPalette(palette);
				reactionContent->document()->setDocumentMargin(0);
				reactionContent->document()->setDefaultFont(overlay.content->document()->defaultFont());
				reactionContent->document()->setDefaultStyleSheet(document()->defaultStyleSheet() +
					QStringLiteral("\nbody { margin: 0; padding: 0; background: transparent; }\n"));
				overlay.reactionContent = reactionContent;
			}
			if (reactionsChanged)
			{
				overlay.reactionContent->setHtml(reactionHtml);
				overlay.renderedReactionHtml = reactionHtml;
			}
			overlay.reactionContent->document()->adjustSize();
			const QSize reactionSize = overlay.reactionContent->document()->size().toSize();
			overlay.reactionContent->setFixedSize(qMax(1, reactionSize.width() + 2),
				qMax(1, reactionSize.height() + 2));
			overlay.reactionContent->show();
		}
		else if (overlay.reactionContent)
		{
			overlay.reactionContent->clear();
			overlay.reactionContent->hide();
			overlay.renderedReactionHtml.clear();
		}

		QSet<QUrl> imageResources = DecorationHelper::imageResourcesFromHtml(renderedHtml);
		imageResources.unite(DecorationHelper::imageResourcesFromHtml(adjacentHtml));
		imageResources.unite(DecorationHelper::imageResourcesFromHtml(reactionHtml));
		overlay.imageResources = imageResources;
		for (const QUrl &url : imageResources)
		{
			const QVariant image = document()->resource(QTextDocument::ImageResource, url);
			if (image.isValid())
			{
				overlay.content->document()->addResource(QTextDocument::ImageResource, url, image);
				if (overlay.sideContent)
					overlay.sideContent->document()->addResource(QTextDocument::ImageResource, url, image);
				if (overlay.reactionContent)
					overlay.reactionContent->document()->addResource(QTextDocument::ImageResource, url, image);
			}
		}
		if (bubbleChanged)
		{
			overlay.content->document()->markContentsDirty(0, overlay.content->document()->characterCount());
			overlay.content->viewport()->update();
		}
		if (overlay.sideContent)
		{
			overlay.sideContent->document()->markContentsDirty(0,
				overlay.sideContent->document()->characterCount());
			overlay.sideContent->viewport()->update();
		}
		if (overlay.reactionContent && reactionsChanged)
		{
			overlay.reactionContent->document()->markContentsDirty(0,
				overlay.reactionContent->document()->characterCount());
			overlay.reactionContent->viewport()->update();
		}
		if (overlay.geometry.initialized)
		{
			const qreal reactionHeight = overlay.reactionContent &&
				!overlay.reactionContent->isHidden()
				? overlay.reactionContent->height() + 3 : 0;
			DecorationHelper::setSourceSpacerHeight(overlay.sourceSpacerCursor,
				overlay.geometry.documentRect.height() + reactionHeight);
		}
		updated = true;
	}
	if (updated)
		scheduleMessageBubbleGeometryUpdate();
	return updated;
}

bool StyleViewer::replaceMessageBubbleContent(const QString &AMessageId, const QString &AHtml)
{
	if (AMessageId.isEmpty())
		return false;

	for (BubbleOverlay &overlay : FBubbleOverlays)
	{
		if (overlay.messageId != AMessageId || !overlay.content)
			continue;

		const QSet<QUrl> imageResources = DecorationHelper::imageResourcesFromHtml(AHtml);
		for (const QUrl &url : imageResources)
		{
			const QVariant image = document()->resource(QTextDocument::ImageResource, url);
			if (image.isValid())
				overlay.content->document()->addResource(QTextDocument::ImageResource, url, image);
		}
		overlay.html = AHtml;
		const QString renderedHtml = DecorationHelper::renderBubbleHtml(overlay.html, overlay.decorations);
		overlay.content->setHtml(QStringLiteral(
			"<div class=\"xxxmessage\" style=\"background-color:transparent;\">%1</div>").arg(renderedHtml));
		for (const QUrl &url : imageResources)
		{
			const QSize imageSize = imageSizeForResource(document()->resource(QTextDocument::ImageResource, url));
			if (!imageSize.isEmpty())
				DecorationHelper::updateImageFormatForResource(*overlay.content->document(), url,
					imageSize, overlay.content->viewport()->width());
		}
		overlay.renderedHtml = renderedHtml;
		overlay.imageResources = imageResources;
		overlay.contentSizeDirty = true;
		scheduleMessageBubbleGeometryUpdate();
		return true;
	}
	return false;
}

void StyleViewer::clearMessageBubbles()
{
	for (const BubbleOverlay &overlay : FBubbleOverlays)
	{
		if (overlay.sideContent)
			delete overlay.sideContent.data();
		if (overlay.reactionContent)
			delete overlay.reactionContent.data();
		if (overlay.frame)
			delete overlay.frame.data();
	}
	FBubbleOverlays.clear();
}

void StyleViewer::updateMessageBubbleResource(const QUrl &AUrl)
{
	const QVariant image = document()->resource(QTextDocument::ImageResource, AUrl);
	if (!image.isValid())
		return;
	const QSize imageSize = imageSizeForResource(image);
	bool geometryUpdateNeeded = false;
	for (BubbleOverlay &overlay : FBubbleOverlays)
	{
		if (!overlay.imageResources.contains(AUrl))
			continue;
		if (overlay.content)
		{
			QTextDocument *doc = overlay.content->document();
			doc->addResource(QTextDocument::ImageResource, AUrl, image);
			if (!imageSize.isEmpty())
				DecorationHelper::updateImageFormatForResource(*doc, AUrl, imageSize,
					overlay.content->viewport()->width());
			doc->markContentsDirty(0, doc->characterCount());
			overlay.contentSizeDirty = true;
		}
		if (overlay.sideContent)
		{
			QTextDocument *doc = overlay.sideContent->document();
			doc->addResource(QTextDocument::ImageResource, AUrl, image);
			doc->markContentsDirty(0, doc->characterCount());
		}
		if (overlay.reactionContent)
		{
			QTextDocument *doc = overlay.reactionContent->document();
			doc->addResource(QTextDocument::ImageResource, AUrl, image);
			doc->markContentsDirty(0, doc->characterCount());
		}
		geometryUpdateNeeded = true;
	}
	if (geometryUpdateNeeded)
		scheduleMessageBubbleGeometryUpdate();
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

void StyleViewer::scheduleMessageBubbleGeometryUpdate()
{
	if (!FGeometryUpdateTimer.isActive())
		FGeometryUpdateTimer.start();
}

void StyleViewer::updateMessageBubbleGeometry()
{
	if (!document() || !document()->documentLayout())
		return;
	const QPointF scrollOffset(horizontalScrollBar()->value(), verticalScrollBar()->value());
	for (int i = 0; i < FBubbleOverlays.size(); )
	{
		BubbleOverlay &overlay = FBubbleOverlays[i];
		if (!overlay.table || !overlay.frame)
		{
			QPointer<QFrame> frame = overlay.frame;
			FBubbleOverlays.removeAt(i);
			if (frame)
				delete frame.data();
			continue;
		}
		const QRectF tableRect = document()->documentLayout()->frameBoundingRect(overlay.table.data());
		const QRectF anchorRect = overlay.sourceAnchorRect.isValid()
			? overlay.sourceAnchorRect : tableRect;
		if (!tableRect.isValid() && !anchorRect.isValid())
		{
			++i;
			continue;
		}

		const bool firstLayout = !overlay.geometry.initialized;
		if (firstLayout)
		{
			QRectF bubbleAnchorRect = anchorRect;
			overlay.frame->setGeometry(bubbleAnchorRect.translated(-scrollOffset).toAlignedRect());
			if (QLayout *layout = overlay.frame->layout())
				layout->activate();

			if (!overlay.content || !overlay.content->document() ||
				!overlay.content->document()->documentLayout())
			{
				++i;
				continue;
			}

			const qreal frameInsets = qMax(0, overlay.frame->width() -
				overlay.content->viewport()->width());
			bubbleAnchorRect = DecorationHelper::bubbleRectForContentWidth(bubbleAnchorRect,
				DecorationHelper::maximumImageDisplayWidth(*overlay.content->document()),
				frameInsets, overlay.outgoing);
			if (bubbleAnchorRect.width() > overlay.frame->width())
			{
				overlay.frame->setGeometry(bubbleAnchorRect.translated(-scrollOffset).toAlignedRect());
				if (QLayout *layout = overlay.frame->layout())
					layout->activate();
			}

			const int textWidth = overlay.content->viewport()->width();
			if (textWidth <= 0)
			{
				++i;
				continue;
			}
			overlay.content->document()->setTextWidth(textWidth);
			const qreal contentHeight = overlay.content->document()->documentLayout()->documentSize().height();
			const QMargins margins = overlay.frame->layout()
				? overlay.frame->layout()->contentsMargins() : QMargins();
			if (!DecorationHelper::initializeBubbleGeometry(overlay.geometry, bubbleAnchorRect,
				contentHeight, margins.top() + margins.bottom()))
			{
				++i;
				continue;
			}
			overlay.contentSizeDirty = false;
			const qreal reactionHeight = overlay.reactionContent &&
				!overlay.reactionContent->isHidden()
				? overlay.reactionContent->height() + 3 : 0;
			DecorationHelper::setSourceSpacerHeight(overlay.sourceSpacerCursor,
				overlay.geometry.documentRect.height() + reactionHeight);
		}
		else
		{
			if (overlay.contentSizeDirty && overlay.content && overlay.content->document() &&
				overlay.content->document()->documentLayout())
			{
				const int textWidth = overlay.content->viewport()->width();
				if (textWidth > 0)
					overlay.content->document()->setTextWidth(textWidth);
				const qreal contentHeight = overlay.content->document()->documentLayout()->documentSize().height();
				const QMargins margins = overlay.frame->layout()
					? overlay.frame->layout()->contentsMargins() : QMargins();
				const QRectF currentRect = overlay.geometry.documentRect;
				DecorationHelper::initializeBubbleGeometry(overlay.geometry, currentRect,
					contentHeight, margins.top() + margins.bottom());
				overlay.contentSizeDirty = false;
				const qreal reactionHeight = overlay.reactionContent &&
					!overlay.reactionContent->isHidden()
					? overlay.reactionContent->height() + 3 : 0;
				DecorationHelper::setSourceSpacerHeight(overlay.sourceSpacerCursor,
					overlay.geometry.documentRect.height() + reactionHeight);
			}
			// Preserve each bubble's size and horizontal anchor. Only preceding flow changes may move it vertically.
			QPointF anchorPosition = overlay.geometry.documentRect.topLeft();
			if (tableRect.isValid())
				anchorPosition.setY(tableRect.top());
			overlay.geometry.documentRect.moveTopLeft(anchorPosition);
		}

		QRectF visibleRect = overlay.geometry.documentRect;
		visibleRect.translate(-scrollOffset);
		const QRect bubbleRect = visibleRect.toAlignedRect();
		if (overlay.frame->geometry() != bubbleRect)
			overlay.frame->setGeometry(bubbleRect);
		if (!overlay.frame->isVisible())
		{
			overlay.frame->show();
			overlay.frame->raise();
		}
		if (overlay.sideContent && !overlay.sideContent->isHidden())
		{
			const QRect statusRect = DecorationHelper::adjacentDecorationRect(bubbleRect,
				overlay.sideContent->size(), viewport()->size(), overlay.outgoing);
			if (statusRect.isValid())
			{
				overlay.sideContent->setGeometry(statusRect);
				overlay.sideContent->raise();
			}
			else
			{
				overlay.sideContent->hide();
			}
		}
		if (overlay.reactionContent && !overlay.reactionContent->isHidden())
		{
			const QRect reactionRect = DecorationHelper::reactionDecorationRect(bubbleRect,
				overlay.reactionContent->size(), viewport()->size(), overlay.outgoing);
			if (reactionRect.isValid())
			{
				overlay.reactionContent->setGeometry(reactionRect);
				overlay.reactionContent->raise();
			}
			else
			{
				overlay.reactionContent->hide();
			}
		}
		++i;
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
	scheduleMessageBubbleGeometryUpdate();
}

void StyleViewer::notifyUserScrollPositionChanged()
{
	QScrollBar *scrollBar = verticalScrollBar();
	emit userScrollPositionChanged(scrollBar->sliderPosition(), scrollBar->maximum());
}
