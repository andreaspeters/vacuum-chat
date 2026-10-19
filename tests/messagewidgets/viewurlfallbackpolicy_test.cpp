#include "viewurlfallbackpolicy.h"

#include <iostream>

int main()
{
	const QUrl internalMediaUrl(QStringLiteral("vacuum-media://open?room=!room:example.org&event=$event"));
	if (ViewUrlFallbackPolicy::shouldOpenWithDesktopServices(internalMediaUrl)) {
		std::cerr << "FAIL: internal media links must not be sent to the desktop URL handler\n";
		return 1;
	}

	const QUrl httpUrl(QStringLiteral("https://example.org/page"));
	const QUrl mailUrl(QStringLiteral("mailto:user@example.org"));
	const QUrl fileUrl(QStringLiteral("file:///tmp/example.png"));
	if (!ViewUrlFallbackPolicy::shouldOpenWithDesktopServices(httpUrl) ||
		!ViewUrlFallbackPolicy::shouldOpenWithDesktopServices(mailUrl) ||
		!ViewUrlFallbackPolicy::shouldOpenWithDesktopServices(fileUrl)) {
		std::cerr << "FAIL: common external URLs must remain eligible for desktop opening\n";
		return 1;
	}

	std::cout << "PASS: desktop fallback rejects internal URLs and permits common external URLs\n";
	return 0;
}
