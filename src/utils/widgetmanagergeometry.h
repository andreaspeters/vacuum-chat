#ifndef WIDGETMANAGERGEOMETRY_H
#define WIDGETMANAGERGEOMETRY_H

#include <QRect>

namespace WidgetManagerGeometry
{
	inline QRect alignRect(const QRect &ARect, const QRect &ABoundary, Qt::Alignment AAlign)
	{
		QRect rect = ARect;
		if (AAlign>0 && !ARect.isEmpty() && !ABoundary.isEmpty())
		{
			if ((AAlign & Qt::AlignLeft) == Qt::AlignLeft)
				rect.moveLeft(ABoundary.left());
			else if ((AAlign & Qt::AlignRight) == Qt::AlignRight)
				rect.moveRight(ABoundary.right());
			else if ((AAlign & Qt::AlignHCenter) == Qt::AlignHCenter)
				rect.moveLeft((ABoundary.width()-ARect.width())/2 + ABoundary.left());

			if ((AAlign & Qt::AlignTop) == Qt::AlignTop)
				rect.moveTop(ABoundary.top());
			else if ((AAlign & Qt::AlignBottom) == Qt::AlignBottom)
				rect.moveBottom(ABoundary.bottom());
			else if ((AAlign & Qt::AlignVCenter) == Qt::AlignVCenter)
				rect.moveTop((ABoundary.height() - ARect.height())/2 + ABoundary.top());
		}
		return rect;
	}
}

#endif // WIDGETMANAGERGEOMETRY_H