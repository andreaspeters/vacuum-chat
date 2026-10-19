#ifndef VIEWURLFALLBACKPOLICY_H
#define VIEWURLFALLBACKPOLICY_H

#include <QSet>
#include <QUrl>

namespace ViewUrlFallbackPolicy
{
inline bool shouldOpenWithDesktopServices(const QUrl &url)
{
	static const QSet<QString> externalSchemes = {
		QStringLiteral("http"),
		QStringLiteral("https"),
		QStringLiteral("mailto"),
		QStringLiteral("ftp"),
		QStringLiteral("file")
	};
	return externalSchemes.contains(url.scheme().toLower());
}
}

#endif // VIEWURLFALLBACKPOLICY_H
