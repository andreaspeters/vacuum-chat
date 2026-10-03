#ifndef UNICODEAVATAR_H
#define UNICODEAVATAR_H

#include <QImage>
#include <QString>

class UnicodeAvatar
{
public:
    static QString getAvatarPath(const QString &username);
    static QImage generateAvatarImage(const QString &unicodeGrapheme, int size = 64);
    static QString extractFirstUnicodeGrapheme(const QString &input);
};

#endif // UNICODEAVATAR_H