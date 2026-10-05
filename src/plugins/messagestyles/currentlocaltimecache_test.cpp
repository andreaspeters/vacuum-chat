#include "currentlocaltimecache.h"

#include <cstdio>

namespace {
bool check(bool condition, const char *message)
{
	if (!condition)
		std::fprintf(stderr, "FAIL: %s\n", message);
	return condition;
}
}

int main()
{
	CurrentLocalTimeCache cache;
	qint64 epochSecond = 1791240000;
	int localTimeCalls = 0;
	const QDateTime firstLocalTime(QDate(2026, 10, 5), QTime(12, 0), QTimeZone::utc());
	const QDateTime nextLocalTime = firstLocalTime.addSecs(1);
	auto epochProvider = [&epochSecond]() { return epochSecond; };
	auto localTimeProvider = [&localTimeCalls, &firstLocalTime, &nextLocalTime]() {
		return ++localTimeCalls == 1 ? firstLocalTime : nextLocalTime;
	};

	bool passed = true;
	const QDateTime first = cache.currentTime(epochProvider, localTimeProvider);
	const QDateTime sameSecond = cache.currentTime(epochProvider, localTimeProvider);
	passed &= check(first == firstLocalTime && sameSecond == firstLocalTime,
		"repeated conversions in one second reuse one local time");
	passed &= check(localTimeCalls == 1,
		"local time provider runs once within the same epoch second");

	++epochSecond;
	const QDateTime nextSecond = cache.currentTime(epochProvider, localTimeProvider);
	passed &= check(nextSecond == nextLocalTime && localTimeCalls == 2,
		"local time cache refreshes when the epoch second changes");
	return passed ? 0 : 1;
}
