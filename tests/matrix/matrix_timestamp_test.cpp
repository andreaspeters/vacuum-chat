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
	const QDateTime cachedHistoryTimestamp = MatrixTimestamps::fromUnixMilliseconds(epochMs);
	const bool historyUsesExplicitUtc = cachedHistoryTimestamp.timeSpec() != Qt::LocalTime &&
		cachedHistoryTimestamp.timeZone() == QTimeZone::utc();
	if (!preservesEpoch)
		std::cerr << "Matrix event timestamp changed its epoch value\n";
	if (!hasExplicitZone)
		std::cerr << "Matrix event timestamp must be represented in explicit UTC\n";
	if (!historyUsesExplicitUtc)
		std::cerr << "Cached Matrix event timestamps must be represented in explicit UTC\n";
	return preservesEpoch && hasExplicitZone && historyUsesExplicitUtc ? 0 : 1;
}
