#ifndef IMAGEPASTE_POLICY_H
#define IMAGEPASTE_POLICY_H

#include <QImage>

inline QImage prepareImageForUpload(const QImage &image, bool originalSize)
{
    if (originalSize || image.height() <= 768)
        return image;
    return image.scaledToHeight(768, Qt::SmoothTransformation);
}

#endif // IMAGEPASTE_POLICY_H
