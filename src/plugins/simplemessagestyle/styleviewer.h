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
#include <QMap>
#include "decorationhelper.h"
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
	void addMessageBubble(QTextTable *ATable, const QString &AMessageId,
		const QString &AHtml, const QColor &AFill, bool AOutgoing = false,
		const QRectF &ASourceAnchorRect = QRectF(),
		const QTextCursor &ASourceSpacerCursor = QTextCursor());
	bool setMessageDecoration(const QString &AMessageId, const QString &ADecorationId,
		const QString &AHtml);
	bool replaceMessageBubbleContent(const QString &AMessageId, const QString &AHtml);
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
		QPointer<QTextBrowser> sideContent;
		QPointer<QTextBrowser> reactionContent;
		QString messageId;
		QString html;
		QString renderedHtml;
		QString renderedReactionHtml;
		QRectF sourceAnchorRect;
		QTextCursor sourceSpacerCursor;
		bool outgoing = false;
		bool contentSizeDirty = false;
		DecorationHelper::BubbleGeometrySnapshot geometry;
		QMap<QString, DecorationHelper::Decoration> decorations;
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
