#ifndef STYLEVIEWER_H
#define STYLEVIEWER_H

#include <QTextBrowser>
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
signals:
	void userScrollPositionChanged(int APosition, int AMaximum);
protected:
	void wheelEvent(QWheelEvent *AEvent) override;
	void keyPressEvent(QKeyEvent *AEvent) override;
private:
	void notifyUserScrollPositionChanged();
};

#endif // STYLEVIEWER_H
