#include "avatarvisualpolicy.h"

#include <QCoreApplication>
#include <QColor>
#include <iostream>

namespace
{
bool check(bool condition, const char *message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QImage source(1, 1, QImage::Format_ARGB32);
    source.setPixelColor(0, 0, QColor(255, 0, 0, 255));

    const QImage offline = AvatarVisualPolicy::forPresence(
        source, IPresence::Offline, true);
    const QColor offlinePixel = offline.pixelColor(0, 0);
    bool passed = check(offlinePixel.red() == offlinePixel.green() &&
        offlinePixel.green() == offlinePixel.blue() &&
        offlinePixel.alpha() > 0 && offlinePixel.alpha() < 255,
        "enabled Offline avatar is desaturated and translucent");

    const QImage online = AvatarVisualPolicy::forPresence(
        source, IPresence::Online, true);
    const QColor onlinePixel = online.pixelColor(0, 0);
    passed &= check(onlinePixel.red() == 255 && onlinePixel.green() == 0 &&
        onlinePixel.blue() == 0 && onlinePixel.alpha() == 255,
        "Online avatar remains unchanged");

    const QImage disabled = AvatarVisualPolicy::forPresence(
        source, IPresence::Offline, false);
    const QColor disabledPixel = disabled.pixelColor(0, 0);
    passed &= check(disabledPixel.red() == 255 && disabledPixel.green() == 0 &&
        disabledPixel.blue() == 0 && disabledPixel.alpha() == 255,
        "disabled gray-avatar option leaves Offline avatar unchanged");

    passed &= check(AvatarVisualPolicy::shouldShowRosterAvatar(false),
        "avatars remain visible on ordinary roster rows");
    passed &= check(!AvatarVisualPolicy::shouldShowRosterAvatar(true),
        "avatars are hidden on favorite copies");

    return passed ? 0 : 1;
}
