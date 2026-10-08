#ifndef AVATARVISUALPOLICY_H
#define AVATARVISUALPOLICY_H

#include <interfaces/ipresence.h>
#include <utils/imagemanager.h>

namespace AvatarVisualPolicy
{
inline bool shouldShowRosterAvatar(bool isRecentFavorite)
{
    return !isRecentFavorite;
}

inline bool shouldGray(int show, bool showGrayAvatars)
{
    return showGrayAvatars &&
        (show == IPresence::Offline || show == IPresence::Error);
}

inline QImage grayImage(const QImage &image)
{
    return ImageManager::opacitized(ImageManager::grayscaled(image));
}

inline QImage forPresence(const QImage &image, int show, bool showGrayAvatars)
{
    return shouldGray(show, showGrayAvatars) ? grayImage(image) : image;
}
}

#endif // AVATARVISUALPOLICY_H
