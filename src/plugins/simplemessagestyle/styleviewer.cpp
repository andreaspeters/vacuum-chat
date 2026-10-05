#include "styleviewer.h"

#include <QAbstractSlider>
#include <QCryptographicHash>
#include <QImage>
#include <QKeyEvent>
#include <QPointer>
#include <QScrollBar>
#include <QTextDocument>
#include <QTimer>
#include <QTextBlock>
#include <QMouseEvent>
#include <QTextDocumentFragment>
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
}

StyleViewer::~StyleViewer()
{

}

QString StyleViewer::cacheImageResource(const QString &APath)
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
				scheduler->loadFile(APath, this, [viewer, key, resourceUrl](const QImage &image) {
					if (!viewer)
						return;
					viewer->FPendingImageResources.remove(key);
					if (image.isNull())
						return;
					QTextDocument *doc = viewer->document();
					doc->addResource(QTextDocument::ImageResource, resourceUrl, image);
					doc->markContentsDirty(0, doc->characterCount());
					viewer->viewport()->update();
				});
			}
		}
	}
	return resourceUrl.toString(QUrl::FullyEncoded);
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

void StyleViewer::notifyUserScrollPositionChanged()
{
	QScrollBar *scrollBar = verticalScrollBar();
	emit userScrollPositionChanged(scrollBar->sliderPosition(), scrollBar->maximum());
}
