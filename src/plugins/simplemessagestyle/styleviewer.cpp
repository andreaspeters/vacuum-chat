#include "styleviewer.h"

#include <QAbstractSlider>
#include <QKeyEvent>
#include <QScrollBar>
#include <QTimer>
#include <QTextBlock>
#include <QMouseEvent>
#include <QTextDocumentFragment>
#include <QWheelEvent>

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
