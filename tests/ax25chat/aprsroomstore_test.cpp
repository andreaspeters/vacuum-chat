#include "aprsroomstore.h"

#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>

#include <iostream>

namespace
{
bool check(bool condition, const char *message)
{
    if (!condition)
        std::cerr << message << '\n';
    return condition;
}
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTemporaryDir tempDir;
    if (!tempDir.isValid())
        return 2;
    const QString path = tempDir.filePath(QStringLiteral("settings.ini"));
    bool passed = true;

    QSettings seed(path, QSettings::IniFormat);
    seed.beginGroup(QStringLiteral("accounts/account-a"));
    seed.setValue(QStringLiteral("contacts"), QStringList{QStringLiteral("DL1AAA")});
    seed.endGroup();
    seed.beginGroup(QStringLiteral("accounts/account-b"));
    seed.setValue(QStringLiteral("aprs.groups"), QStringList{QStringLiteral("WIDE")});
    seed.endGroup();
    seed.sync();

    passed &= check(AprsRoom::GroupStore::addGroup(path, QStringLiteral("account-a"),
                                                    QStringLiteral("  sota  ")),
                    "valid group names must be accepted and persisted");
    passed &= check(AprsRoom::GroupStore::addGroup(path, QStringLiteral("account-a"),
                                                    QStringLiteral("SOTA")),
                    "adding an existing group must be idempotent");
    passed &= check(AprsRoom::GroupStore::groupNames(path, QStringLiteral("account-a")) ==
                        QStringList{QStringLiteral("SOTA")},
                    "stored group names must be canonical and unique");
    passed &= check(AprsRoom::GroupStore::groupNames(path, QStringLiteral("account-b")) ==
                        QStringList{QStringLiteral("WIDE")},
                    "group subscriptions must be isolated by account");

    const QString maxName(46, QLatin1Char('A'));
    passed &= check(AprsRoom::GroupStore::addGroup(path, QStringLiteral("account-a"), maxName),
                    "the codec's maximum-length group name must persist");
    passed &= check(!AprsRoom::GroupStore::addGroup(path, QStringLiteral("account-a"),
                                                     QString(47, QLatin1Char('A'))),
                    "group names beyond the codec limit must be rejected");
    passed &= check(!AprsRoom::GroupStore::addGroup(path, QStringLiteral("account-a"),
                                                     QStringLiteral("TWO WORDS")),
                    "group names containing spaces must be rejected");

    passed &= check(AprsRoom::GroupStore::removeGroup(path, QStringLiteral("account-a"),
                                                       QStringLiteral("sota")),
                    "stored groups must be removable case-insensitively");
    passed &= check(AprsRoom::GroupStore::groupNames(path, QStringLiteral("account-a")) ==
                        QStringList{maxName},
                    "removing a group must preserve other groups");
    QSettings verify(path, QSettings::IniFormat);
    verify.beginGroup(QStringLiteral("accounts/account-a"));
    passed &= check(verify.value(QStringLiteral("contacts")).toStringList() ==
                        QStringList{QStringLiteral("DL1AAA")},
                    "group updates must preserve existing contact settings");
    return passed ? 0 : 1;
}
