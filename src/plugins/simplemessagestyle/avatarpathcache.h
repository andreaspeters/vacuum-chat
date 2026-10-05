#ifndef AVATARPATHCACHE_H
#define AVATARPATHCACHE_H

#include <QSet>
#include <QString>

class AvatarPathExistsCache
{
public:
	template<typename ExistsProbe>
	bool exists(const QString &APath, ExistsProbe AProbe) const
	{
		if (APath.isEmpty())
			return false;
		if (FExistingPaths.contains(APath))
			return true;
		if (!AProbe(APath))
			return false;
		FExistingPaths.insert(APath);
		return true;
	}

private:
	mutable QSet<QString> FExistingPaths;
};

#endif // AVATARPATHCACHE_H
