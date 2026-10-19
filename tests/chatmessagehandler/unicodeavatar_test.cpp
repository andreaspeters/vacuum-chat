#include "unicodeavatar.h"

#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QTemporaryDir>

#include <iostream>

namespace
{
bool check(bool condition, const char *description)
{
    if (!condition)
        std::cerr << "FAIL: " << description << '\n';
    return condition;
}
}

int main(int argc, char **argv)
{
    QTemporaryDir cacheRoot;
    if (!cacheRoot.isValid())
        return 2;
    qputenv("XDG_CACHE_HOME", cacheRoot.path().toLocal8Bit());

    QGuiApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("VacuumTests"));
    QCoreApplication::setApplicationName(QStringLiteral("UnicodeAvatarTests"));

    bool passed = true;
    const QString familyEmoji = QString::fromUtf8("👨‍👩‍👧‍👦");
    passed &= check(UnicodeAvatar::extractFirstUnicodeGrapheme(
                        QStringLiteral("Alice 🌟 Bob")) == QString::fromUtf8("🌟"),
                    "extracts a standalone emoji grapheme");
    passed &= check(UnicodeAvatar::extractFirstUnicodeGrapheme(
                        familyEmoji + QStringLiteral(" Family")) == familyEmoji,
                    "keeps a multi-codepoint emoji grapheme intact");
    passed &= check(UnicodeAvatar::extractFirstUnicodeGrapheme(
                        QStringLiteral("José")) == QString::fromUtf8("é"),
                    "extracts a non-ASCII grapheme from a name");
    passed &= check(UnicodeAvatar::extractFirstUnicodeGrapheme(
                        QStringLiteral("Alice")).isEmpty(),
                    "does not generate a Unicode avatar for ASCII-only names");

    QString username = QString::fromUtf8("🌻Alice");
    const QString originalUsername = username;
    const QString avatarPath = UnicodeAvatar::getAvatarPath(username);
    passed &= check(username == originalUsername,
                    "avatar creation does not mutate the displayed username");
    passed &= check(!avatarPath.isEmpty() && QFile::exists(avatarPath),
                    "creates a PNG avatar file in the cache");
    const QImage avatar(avatarPath);
    passed &= check(!avatar.isNull(), "generated avatar PNG can be loaded");

    return passed ? 0 : 1;
}