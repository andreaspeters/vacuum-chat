#include "matrixnetwork.h"

#include <iostream>
#include <QTimeZone>

int main()
{
	const qint64 epochMs = 1760000000000LL;
	MatrixTextEvent event;
	event.eventId = QStringLiteral("$timestamp-test");
	event.roomId = QStringLiteral("!room:test");
	event.userId = QStringLiteral("@user:test");
	event.timestamp = QString::number(epochMs);

	const BasicMessage message = event.toBasicMessage();
	const bool preservesEpoch = message.timestamp().toMSecsSinceEpoch() == epochMs;
	const bool hasExplicitZone = message.timestamp().timeSpec() != Qt::LocalTime &&
		message.timestamp().timeZone() == QTimeZone::utc();
	if (!preservesEpoch)
		std::cerr << "Matrix event timestamp changed its epoch value\n";
	if (!hasExplicitZone)
		std::cerr << "Matrix event timestamp must be represented in explicit UTC\n";
	return preservesEpoch && hasExplicitZone ? 0 : 1;
}
