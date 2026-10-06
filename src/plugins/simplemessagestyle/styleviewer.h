#ifndef STYLEVIEWER_H
#define STYLEVIEWER_H

#include <QTextBrowser>
#include <QColor>
#include <QFrame>
#include <QList>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QTextDocumentFragment>
#include <QTextTable>
#include <utils/animatedtextbrowser.h>

class QKeyEvent;
class QWheelEvent;

class StyleViewer: 
	public AnimatedTextBrowser
{
	Q_OBJECT;
public:
	StyleViewer(QWidget *AParent);
	~StyleViewer();
	QString cacheImageResource(const QString &APath, bool ARoundCorners = false);
	void addMessageBubble(QTextTable *ATable, const QString &AHtml, const QColor &AFill);
	void clearMessageBubbles();
	QTextDocumentFragment bubbleSelection() const;
	QTextDocumentFragment bubbleTextUnderPosition(const QPoint &APosition) const;
signals:
	void userScrollPositionChanged(int APosition, int AMaximum);
	void bubbleAnchorClicked(const QUrl &AUrl);
protected:
	void wheelEvent(QWheelEvent *AEvent) override;
	void keyPressEvent(QKeyEvent *AEvent) override;
	void resizeEvent(QResizeEvent *AEvent) override;
private:
	struct BubbleOverlay {
		QPointer<QTextTable> table;
		QPointer<QFrame> frame;
		QPointer<QTextBrowser> content;
		QSet<QUrl> imageResources;
	};
	void notifyUserScrollPositionChanged();
	void updateMessageBubbleGeometry();
	void updateMessageBubbleResource(const QUrl &AUrl);
	void scheduleMessageBubbleGeometryUpdate();
	QSet<QString> FPendingImageResources;
	QList<BubbleOverlay> FBubbleOverlays;
	QTimer FGeometryUpdateTimer;
};

#endif // STYLEVIEWER_H
