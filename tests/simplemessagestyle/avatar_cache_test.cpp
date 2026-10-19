#include "avatarpathcache.h"

#include <iostream>

namespace {
bool check(bool ACondition, const char *ADescription)
{
	if (!ACondition)
		std::cerr << ADescription << " failed\n";
	return ACondition;
}
}

int main()
{
	bool passed = true;
	AvatarPathExistsCache cache;
	const QString avatarPath = QStringLiteral("/cache/avatar-hash.bin");
	int existingPathProbes = 0;
	auto existingPathProbe = [&existingPathProbes](const QString &APath) {
		++existingPathProbes;
		return APath == QStringLiteral("/cache/avatar-hash.bin");
	};

	passed &= check(cache.exists(avatarPath, existingPathProbe),
		"an existing avatar path is accepted");
	passed &= check(cache.exists(avatarPath, existingPathProbe),
		"a repeated avatar path remains available");
	passed &= check(existingPathProbes == 1,
		"a repeated avatar path triggers only one filesystem probe");

	AvatarPathExistsCache lateFileCache;
	bool fileAvailable = false;
	int lateFileProbes = 0;
	auto lateFileProbe = [&fileAvailable, &lateFileProbes](const QString &) {
		++lateFileProbes;
		return fileAvailable;
	};
	passed &= check(!lateFileCache.exists(avatarPath, lateFileProbe),
		"a missing avatar path is reported unavailable");
	fileAvailable = true;
	passed &= check(lateFileCache.exists(avatarPath, lateFileProbe),
		"a newly downloaded avatar path becomes available");
	passed &= check(lateFileProbes == 2,
		"negative results are not cached across an avatar download");

	bool emptyPathProbed = false;
	passed &= check(!cache.exists(QString(), [&emptyPathProbed](const QString &) {
		emptyPathProbed = true;
		return false;
	}), "an empty avatar path is unavailable");
	passed &= check(!emptyPathProbed, "an empty path does not touch the filesystem");

	return passed ? 0 : 1;
}
