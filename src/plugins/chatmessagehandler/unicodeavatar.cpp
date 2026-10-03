#include "unicodeavatar.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextBoundaryFinder>

QString UnicodeAvatar::getAvatarPath(const QString &username)
{
    const QString grapheme = extractFirstUnicodeGrapheme(username);
    if (grapheme.isEmpty())
        return QString();

    const QString cacheRoot = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (cacheRoot.isEmpty())
        return QString();

    const QString avatarDirectory = QDir(cacheRoot).filePath(QStringLiteral("unicode-avatars"));
    if (!QDir().mkpath(avatarDirectory))
        return QString();

    const QByteArray digest = QCryptographicHash::hash(grapheme.toUtf8(), QCryptographicHash::Sha256).toHex();
    const QString path = QDir(avatarDirectory).filePath(QString::fromLatin1(digest) + QStringLiteral(".png"));
    if (QFileInfo::exists(path)) {
        QImageReader reader(path);
        if (reader.canRead())
            return path;
        QFile::remove(path);
    }

    const QImage image = generateAvatarImage(grapheme);
    if (image.isNull())
        return QString();

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG") || !file.commit())
        return QString();
    return path;
}

QImage UnicodeAvatar::generateAvatarImage(const QString &unicodeGrapheme, int size)
{
    if (unicodeGrapheme.isEmpty() || size <= 0)
        return QImage();

    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    const QByteArray digest = QCryptographicHash::hash(unicodeGrapheme.toUtf8(), QCryptographicHash::Sha256);
    const int hue = ((static_cast<unsigned char>(digest.at(0)) << 8) |
                     static_cast<unsigned char>(digest.at(1))) % 360;
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor::fromHsv(hue, 150, 190));
    painter.drawEllipse(image.rect());

    QFont font = painter.font();
    font.setPixelSize(qMax(12, size * 2 / 3));
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(image.rect(), Qt::AlignCenter, unicodeGrapheme);
    return image;
}

QString UnicodeAvatar::extractFirstUnicodeGrapheme(const QString &input)
{
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, input);
    int start = 0;
    int end = finder.toNextBoundary();
    while (end >= 0) {
        for (int index = start; index < end; ++index) {
            if (input.at(index).unicode() > 0x7f)
                return input.mid(start, end - start);
        }
        start = end;
        end = finder.toNextBoundary();
    }
    return QString();
}