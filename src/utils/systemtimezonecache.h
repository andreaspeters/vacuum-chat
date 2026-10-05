#ifndef SYSTEMTIMEZONECACHE_H
#define SYSTEMTIMEZONECACHE_H

#include <QDateTime>
#include <QByteArray>
#include <QTimeZone>

namespace SystemTimeZoneCache {
inline bool matchesLocalTime(const QDateTime &utcTime, const QTimeZone &timeZone)
{
	const QDateTime expected = utcTime.toLocalTime();
	const QDateTime actual = utcTime.toTimeZone(timeZone);
	return actual.date() == expected.date() && actual.time() == expected.time() &&
		actual.offsetFromUtc() == expected.offsetFromUtc();
}

inline bool matchesLocalTimeSamples(const QTimeZone &timeZone)
{
	if (!timeZone.isValid())
		return false;
	const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
	const int year = nowUtc.date().year();
	return matchesLocalTime(nowUtc, timeZone) &&
		matchesLocalTime(QDateTime(QDate(year, 1, 15), QTime(12, 0), QTimeZone::utc()), timeZone) &&
		matchesLocalTime(QDateTime(QDate(year, 7, 15), QTime(12, 0), QTimeZone::utc()), timeZone);
}

inline QTimeZone processLocalTimeZone()
{
	const QTimeZone systemTimeZone = QTimeZone::systemTimeZone();
	if (matchesLocalTimeSamples(systemTimeZone))
		return systemTimeZone;

	QByteArray timeZoneId = qgetenv("TZ");
	if (timeZoneId.startsWith(':'))
		timeZoneId.remove(0, 1);
	if (!timeZoneId.isEmpty()) {
		const QTimeZone environmentTimeZone(timeZoneId);
		if (matchesLocalTimeSamples(environmentTimeZone))
			return environmentTimeZone;
	}

	const QTimeZone currentOffset = QTimeZone::fromSecondsAheadOfUtc(
		QDateTime::currentDateTime().offsetFromUtc());
	return matchesLocalTimeSamples(currentOffset) || !systemTimeZone.isValid()
		? currentOffset : systemTimeZone;
}

// Cache the resolved process-local zone so timestamp rendering does not
// re-probe /etc/localtime for every message.
template <typename Provider>
inline const QTimeZone &cachedTimeZone(Provider provider)
{
	static const QTimeZone timeZone = provider();
	return timeZone;
}

template <typename Provider>
inline QDateTime toLocalTime(const QDateTime &dateTime, Provider provider)
{
	if (!dateTime.isValid())
		return dateTime;
	const QTimeZone &timeZone = cachedTimeZone(provider);
	return timeZone.isValid() ? dateTime.toTimeZone(timeZone) : dateTime.toLocalTime();
}

inline QDateTime toSystemLocalTime(const QDateTime &dateTime)
{
	return toLocalTime(dateTime, []() { return processLocalTimeZone(); });
}
}

#endif // SYSTEMTIMEZONECACHE_H
