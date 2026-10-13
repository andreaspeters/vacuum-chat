#include "matriximagedisplay.h"

#include <cstdio>

namespace
{
bool check(bool condition, const char *description)
{
	if (!condition)
		std::fprintf(stderr, "FAIL: %s\n", description);
	return condition;
}
}

int main()
{
	bool passed = true;
	passed &= check(MatrixImageDisplay::sizeForDisplay(QSize(1600, 900), 1000) == QSize(200, 113),
		"large images use a fixed 200 px width and preserve their aspect ratio");
	passed &= check(MatrixImageDisplay::sizeForDisplay(QSize(400, 200), 80) == QSize(200, 100),
		"image width remains 200 px even in a narrow viewport");
	passed &= check(MatrixImageDisplay::sizeForDisplay(QSize(80, 40), 1000) == QSize(200, 100),
		"small images are enlarged to 200 px while preserving their aspect ratio");
	passed &= check(MatrixImageDisplay::sizeForDisplay(QSize(), 800).isEmpty(),
		"invalid source dimensions produce no display size");
	passed &= check(MatrixImageDisplay::sizeForViewer(QSize(1600, 900), QSize(480, 300)) == QSize(480, 270),
		"viewer scales images to fit its viewport without distorting their aspect ratio");
	passed &= check(MatrixImageDisplay::sizeForViewer(QSize(1600, 900), QSize()).isEmpty(),
		"viewer returns no size for an empty viewport");
	const QSize initialSize = MatrixImageDisplay::initialViewerSize(QSize(1600, 900));
	passed &= check(initialSize == QSize(500, 350),
		"large images open in a viewer no larger than 500x400");
	const QSize smallInitialSize = MatrixImageDisplay::initialViewerSize(QSize(80, 40));
	passed &= check(smallInitialSize.width() <= 500 && smallInitialSize.height() <= 400,
		"small images also respect the viewer startup bounds");
	const QUrl imageLink = MatrixImageDisplay::viewerUrl(QStringLiteral("!room:example.org"),
		QStringLiteral("$event:example.org"));
	const QUrlQuery imageLinkQuery(imageLink);
	passed &= check(imageLink.scheme() == QStringLiteral("vacuum-media") &&
		imageLink.host() == QStringLiteral("open") &&
		imageLinkQuery.queryItemValue(QStringLiteral("room")) == QStringLiteral("!room:example.org") &&
		imageLinkQuery.queryItemValue(QStringLiteral("event")) == QStringLiteral("$event:example.org"),
		"image preview open actions use the shared media scheme without exposing remote URLs");
	const QString mxcUrl = QStringLiteral("mxc://media.example.org/an-image");
	const QUrl loadLink = MatrixImageDisplay::mediaLoadUrl(QStringLiteral("!room:example.org"),
		QStringLiteral("$event:example.org"), mxcUrl, QStringLiteral("m.image"),
		QStringLiteral("image & one.png"), true);
	const QUrlQuery loadLinkQuery(loadLink);
	passed &= check(loadLink.scheme() == QStringLiteral("vacuum-media") &&
		loadLink.host() == QStringLiteral("load") &&
		loadLinkQuery.queryItemValue(QStringLiteral("room")) == QStringLiteral("!room:example.org") &&
		loadLinkQuery.queryItemValue(QStringLiteral("event")) == QStringLiteral("$event:example.org") &&
		loadLinkQuery.queryItemValue(QStringLiteral("url")) == mxcUrl &&
		loadLinkQuery.queryItemValue(QStringLiteral("msgtype")) == QStringLiteral("m.image") &&
		loadLinkQuery.queryItemValue(QStringLiteral("body")) == QStringLiteral("image & one.png") &&
		loadLinkQuery.queryItemValue(QStringLiteral("historical")) == QStringLiteral("true"),
		"image media-load links carry encoded room, event, MXC URL, type, body and history state");
	const QString previewHtml = MatrixImageDisplay::previewHtml(imageLink,
		QUrl(QStringLiteral("vacuum-local-image:/resource-id")), QStringLiteral("image<&.png"), QSize(1600, 900));
	passed &= check(previewHtml.contains(QStringLiteral("href=\"vacuum-media://open?")) &&
		previewHtml.contains(QStringLiteral("src=\"vacuum-local-image:/resource-id\"")) &&
		previewHtml.contains(QStringLiteral("alt=\"image&lt;&amp;.png\"")) &&
		previewHtml.contains(QStringLiteral("width=\"200\" height=\"113\"")),
		"image preview HTML uses the safe click URI, escaped alt text and fixed-width dimensions");
	return passed ? 0 : 1;
}
