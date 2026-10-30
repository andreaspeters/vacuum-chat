#include "utils/widgetmanager.h"

#include <iostream>

int main()
{
	const QRect boundary(100, 100, 200, 200);
	const QRect original(0, 0, 50, 50);
	const QRect expected((boundary.width() - original.width()) / 2 + boundary.left(),
		(boundary.height() - original.height()) / 2 + boundary.top(),
		original.width(), original.height());
	const QRect actual = WidgetManager::alignRect(original, boundary,
		Qt::AlignHCenter | Qt::AlignVCenter);

	if (actual != expected)
	{
		std::cerr << "center alignment with offset boundary: expected "
			<< expected.x() << ',' << expected.y() << " but got "
			<< actual.x() << ',' << actual.y() << '\n';
		return 1;
	}
	return 0;
}