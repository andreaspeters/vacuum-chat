#include <utils/systemtimezonecache.h>

#include <cstdio>

namespace {
bool check(bool condition, const char *message)
{
	if (!condition)
		std::fprintf(stderr, "FAIL: %s\n", message);
	return condition;
}

bool sameLocalTime(const QDateTime &actual, const QDateTime &expected)
{
	return actual.date() == expected.date() && actual.time() == expected.time() &&
		actual.offsetFromUtc() == expected.offsetFromUtc();
}

struct CountingTimeZoneProvider
{
	int *calls;

	QTimeZone operator()() const
	{
		++*calls;
		return QTimeZone::utc();
	}
};
}

int main()
{
	bool passed = true;
	int providerCalls = 0;
	const CountingTimeZoneProvider provider{&providerCalls};
	const QDateTime invalid;
	passed &= check(!SystemTimeZoneCache::toLocalTime(invalid, provider).isValid(),
		"invalid timestamps remain invalid");
	passed &= check(providerCalls == 0, "invalid timestamps do not resolve a time zone");

	const QDateTime source = QDateTime::fromMSecsSinceEpoch(1791240000000LL, QTimeZone::utc());
	const QDateTime first = SystemTimeZoneCache::toLocalTime(source, provider);
	const QDateTime second = SystemTimeZoneCache::toLocalTime(source.addSecs(3600), provider);
	passed &= check(providerCalls == 1, "time-zone provider is evaluated once for repeated conversions");
	passed &= check(first.toMSecsSinceEpoch() == source.toMSecsSinceEpoch() &&
		second.toMSecsSinceEpoch() == source.addSecs(3600).toMSecsSinceEpoch(),
		"cached-zone conversion preserves timestamp instants");

	const QDateTime systemLocal = SystemTimeZoneCache::toSystemLocalTime(source);
	const QDateTime expectedSystemLocal = source.toLocalTime();
	passed &= check(sameLocalTime(systemLocal, expectedSystemLocal),
		"cached system-zone conversion matches Qt local wall-clock output");
	QDateTime winterSource(QDate(2026, 1, 15), QTime(12, 0), QTimeZone::utc());
	const QDateTime summerSource(QDate(2026, 7, 15), QTime(12, 0), QTimeZone::utc());
	passed &= check(sameLocalTime(SystemTimeZoneCache::toSystemLocalTime(winterSource),
		winterSource.toLocalTime()), "cached zone preserves winter local-time behavior");
	passed &= check(sameLocalTime(SystemTimeZoneCache::toSystemLocalTime(summerSource),
		summerSource.toLocalTime()), "cached zone preserves summer local-time behavior");
	return passed ? 0 : 1;
}
