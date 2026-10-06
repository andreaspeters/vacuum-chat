#include <utils/roundedavatar.h>
#include "roomsidebarstate.h"

#include <QColor>
#include <QImage>
#include <QApplication>
#include <QLabel>
#include <QPixmap>
#include <iostream>

static bool check(bool condition, const char *description)
{
    if (!condition)
        std::cerr << "FAIL: " << description << std::endl;
    return condition;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
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

    const QSize displaySizes[] = {QSize(32, 32), QSize(21, 21), QSize(61, 61)};
    for (const QSize &displaySize : displaySizes) {
        const QImage displayAvatar = RoundedAvatar::roundImageScaled(nonSquare, displaySize);
        passed &= check(displayAvatar.size() == displaySize,
                        "scales non-square avatars to the exact requested display size");
        passed &= check(displayAvatar.pixelColor(0, 0).alpha() < 64 &&
                        displayAvatar.pixelColor(displaySize.width() / 2,
                            displaySize.height() / 2).alpha() > 240,
                        "preserves rounded corners and the opaque center at each display size");
    }
    passed &= check(nonSquare.pixelColor(0, 0) == fill,
                    "display scaling does not mutate the source avatar");

    QImage highDpiSource(QSize(22, 22), QImage::Format_ARGB32_Premultiplied);
    highDpiSource.fill(fill);
    highDpiSource.setDevicePixelRatio(2.0);
    const QPixmap reusedAvatar = QPixmap::fromImage(highDpiSource);
    passed &= check(reusedAvatar.deviceIndependentSize() == QSizeF(11.0, 11.0),
                    "reproduces an 11x11 high-DPI pixmap retained by the sidebar");
    const QPixmap resizedReusedAvatar = RoomSidebarState::avatarPixmapForDisplay(
        reusedAvatar, QSize(21, 21));
    passed &= check(resizedReusedAvatar.size() == QSize(21, 21) &&
                    resizedReusedAvatar.deviceIndependentSize() == QSizeF(21.0, 21.0),
                    "reused member avatar is exactly 21x21 device-independent pixels");
    passed &= check(highDpiSource.devicePixelRatio() == 2.0,
                    "resizing a reused avatar does not mutate the cached source DPR");
    const QImage resizedReusedImage = resizedReusedAvatar.toImage();
    passed &= check(resizedReusedImage.pixelColor(0, 0).alpha() < 64 &&
                    resizedReusedImage.pixelColor(10, 10).alpha() > 240,
                    "reused member avatar retains rounded corners and opaque center");

    QImage smallMemberSource(QSize(13, 13), QImage::Format_ARGB32_Premultiplied);
    smallMemberSource.fill(fill);
    const QPixmap smallMemberPixmap = QPixmap::fromImage(
        RoundedAvatar::roundImage(smallMemberSource, 0.18));
    const QPixmap displayMemberPixmap = RoomSidebarState::avatarPixmapForDisplay(
        smallMemberPixmap, QSize(21, 21));
    QLabel memberAvatarLabel;
    RoomSidebarState::configureMemberAvatarLabel(&memberAvatarLabel);
    memberAvatarLabel.setPixmap(displayMemberPixmap);
    const QPixmap assignedPixmap = memberAvatarLabel.pixmap(Qt::ReturnByValue);
    passed &= check(assignedPixmap.size() == QSize(21, 21) &&
                    assignedPixmap.devicePixelRatio() == 1.0 &&
                    assignedPixmap.deviceIndependentSize() == QSizeF(21.0, 21.0),
                    "member QLabel receives an actual 21x21 pixel pixmap");
    passed &= check(!memberAvatarLabel.hasScaledContents(),
                    "member avatar size is provided by its pixmap, not QLabel scaling");
    memberAvatarLabel.show();
    app.processEvents();
    QImage renderedMemberAvatar(QSize(21, 21), QImage::Format_ARGB32_Premultiplied);
    renderedMemberAvatar.fill(Qt::transparent);
    memberAvatarLabel.render(&renderedMemberAvatar);
    passed &= check(renderedMemberAvatar.pixelColor(1, 10).alpha() > 240 &&
                    renderedMemberAvatar.pixelColor(19, 10).alpha() > 240,
                    "the actual 21x21 member pixmap renders across the widget");

    return passed ? 0 : 1;
}
