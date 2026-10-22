#include "matrixmemberdisplaynamecache.h"

#include <QCoreApplication>
#include <QList>
#include <QString>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    MatrixMemberDisplayNameCache cache;
    int loadCount = 0;
    const auto loadHistory = [&loadCount](const QString &roomId, const QString &userId) {
        ++loadCount;
        if (roomId != QStringLiteral("!room:example.org") ||
            userId != QStringLiteral("@alice:example.org"))
            return QList<MatrixMemberDisplayNameVersion>();
        return QList<MatrixMemberDisplayNameVersion>{
            {100, QStringLiteral("Alice One")},
            {200, QStringLiteral("Alice Two")}};
    };

    if (!cache.displayName(QStringLiteral("!room:example.org"),
                           QStringLiteral("@alice:example.org"), 99, loadHistory).isEmpty())
        return 1;
    if (cache.displayName(QStringLiteral("!room:example.org"),
                          QStringLiteral("@alice:example.org"), 150, loadHistory) !=
        QStringLiteral("Alice One"))
        return 2;
    if (cache.displayName(QStringLiteral("!room:example.org"),
                          QStringLiteral("@alice:example.org"), 250, loadHistory) !=
        QStringLiteral("Alice Two"))
        return 3;
    if (loadCount != 1)
        return 4;
    return 0;
}
