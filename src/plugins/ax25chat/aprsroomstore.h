#ifndef APRSROOMSTORE_H
#define APRSROOMSTORE_H

#include <QString>
#include <QStringList>

namespace AprsRoom
{

// Persists the user's desired ANSRVR subscriptions, not server-confirmed membership.
class GroupStore
{
public:
    static QStringList groupNames(const QString &settingsFilePath, const QString &accountId);
    static bool addGroup(const QString &settingsFilePath, const QString &accountId,
                         const QString &groupName, QString *error = nullptr);
    static bool removeGroup(const QString &settingsFilePath, const QString &accountId,
                            const QString &groupName, QString *error = nullptr);
};

} // namespace AprsRoom

#endif // APRSROOMSTORE_H
