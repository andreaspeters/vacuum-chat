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
	passed &= check(MatrixImageDisplay::sizeForDisplay(QSize(1600, 900), 1000) == QSize(400, 225),
		"large images use a fixed 400 px width and preserve their aspect ratio");
	passed &= check(MatrixImageDisplay::sizeForDisplay(QSize(400, 200), 200) == QSize(400, 200),
		"image width remains 400 px even in a narrow viewport");
	passed &= check(MatrixImageDisplay::sizeForDisplay(QSize(80, 40), 1000) == QSize(400, 200),
		"small images are enlarged to 400 px while preserving aspect ratio");
	passed &= check(MatrixImageDisplay::sizeForDisplay(QSize(), 800).isEmpty(),
		"invalid source dimensions produce no display size");
	return passed ? 0 : 1;
}
