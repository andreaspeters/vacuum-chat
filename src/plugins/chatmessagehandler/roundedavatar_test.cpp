#include <utils/roundedavatar.h>

#include <QColor>
#include <QImage>
#include <iostream>

static bool check(bool condition, const char *description)
{
    if (!condition)
        std::cerr << "FAIL: " << description << std::endl;
    return condition;
}

int main()
{
    bool passed = true;
    const QColor fill(48, 112, 176, 255);
    QImage source(QSize(32, 32), QImage::Format_ARGB32_Premultiplied);
    source.fill(fill);

    const QImage rounded = RoundedAvatar::roundImage(source, 0.18);
    passed &= check(rounded.size() == source.size(), "keeps the source avatar dimensions");
    passed &= check(rounded.pixelColor(0, 0).alpha() < 64 &&
                    rounded.pixelColor(31, 0).alpha() < 64 &&
                    rounded.pixelColor(0, 31).alpha() < 64 &&
                    rounded.pixelColor(31, 31).alpha() < 64,
                    "rounds all four display-image corners");
    passed &= check(rounded.pixelColor(16, 16) == fill,
                    "preserves the fully covered center pixels");
    passed &= check(rounded.pixelColor(16, 0).alpha() > 240 &&
                    rounded.pixelColor(31, 16).alpha() > 240 &&
                    rounded.pixelColor(16, 31).alpha() > 240 &&
                    rounded.pixelColor(0, 16).alpha() > 240,
                    "preserves the middle of every edge");
    passed &= check(source.pixelColor(0, 0) == fill,
                    "does not mutate the original cached/source image");

    QImage nonSquare(QSize(40, 24), QImage::Format_ARGB32_Premultiplied);
    nonSquare.fill(fill);
    const QImage roundedNonSquare = RoundedAvatar::roundImage(nonSquare, 0.18);
    passed &= check(roundedNonSquare.size() == nonSquare.size() &&
                    roundedNonSquare.pixelColor(0, 0).alpha() < 64 &&
                    roundedNonSquare.pixelColor(20, 0).alpha() > 240,
                    "rounds non-square avatars using their shorter dimension");

    return passed ? 0 : 1;
}
