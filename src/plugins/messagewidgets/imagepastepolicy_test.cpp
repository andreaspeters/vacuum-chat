#include "imagepastepolicy.h"

#include <QImage>
#include <iostream>

namespace {
bool check(bool condition, const char *message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}
}

int main()
{
    QImage large(400, 1200, QImage::Format_ARGB32);
    large.fill(Qt::red);
    const QImage original = prepareImageForUpload(large, true);
    const QImage reduced = prepareImageForUpload(large, false);

    bool passed = true;
    passed &= check(original.size() == QSize(400, 1200),
        "original-size option preserves the image dimensions");
    passed &= check(reduced.height() == 768,
        "memory-saving option caps image height at 768 pixels");
    passed &= check(reduced.width() == 256,
        "height cap preserves the original aspect ratio");
    const QImage small = prepareImageForUpload(QImage(320, 200, QImage::Format_ARGB32), false);
    passed &= check(small.size() == QSize(320, 200),
        "memory-saving option does not upscale smaller images");
    return passed ? 0 : 1;
}
