#include "aprsroomstore.h"

#include "aprsroomcodec.h"

#include <QSettings>

#include <algorithm>

namespace
{
const QString kGroupsKey = QStringLiteral("aprs.groups");

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

bool validStore(const QString &settingsFilePath, const QString &accountId, QString *error)
{
    if (settingsFilePath.isEmpty() || accountId.trimmed().isEmpty()) {
        setError(error, QStringLiteral("settings path and account ID are required"));
        return false;
    }
    return true;
}

QString canonicalGroupName(const QString &groupName)
{
    const QString canonical = groupName.trimmed().toUpper();
    return AprsRoom::Codec::validateRoomName(canonical) ? canonical : QString();
}

QStringList readGroups(const QString &settingsFilePath, const QString &accountId)
{
    QSettings settings(settingsFilePath, QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("accounts/%1").arg(accountId));
    const QStringList stored = settings.value(kGroupsKey).toStringList();
    settings.endGroup();

    QStringList result;
    for (const QString &name : stored) {
        const QString canonical = canonicalGroupName(name);
        if (!canonical.isEmpty() && !result.contains(canonical))
            result.append(canonical);
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

bool writeGroups(const QString &settingsFilePath, const QString &accountId,
                 const QStringList &groups, QString *error)
{
    QSettings settings(settingsFilePath, QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("accounts/%1").arg(accountId));
    settings.setValue(kGroupsKey, groups);
    settings.endGroup();
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        setError(error, QStringLiteral("could not persist ANSRVR group list"));
        return false;
    }
    return true;
}
} // namespace

namespace AprsRoom
{
QStringList GroupStore::groupNames(const QString &settingsFilePath, const QString &accountId)
{
    if (settingsFilePath.isEmpty() || accountId.trimmed().isEmpty())
        return QStringList();
    return readGroups(settingsFilePath, accountId);
}

bool GroupStore::addGroup(const QString &settingsFilePath, const QString &accountId,
                          const QString &groupName, QString *error)
{
    if (error)
        error->clear();
    if (!validStore(settingsFilePath, accountId, error))
        return false;
    const QString canonical = canonicalGroupName(groupName);
    if (canonical.isEmpty()) {
        setError(error, QStringLiteral("invalid ANSRVR group name"));
        return false;
    }
    QStringList groups = readGroups(settingsFilePath, accountId);
    if (!groups.contains(canonical))
        groups.append(canonical);
    groups.sort(Qt::CaseInsensitive);
    return writeGroups(settingsFilePath, accountId, groups, error);
}

bool GroupStore::removeGroup(const QString &settingsFilePath, const QString &accountId,
                             const QString &groupName, QString *error)
{
    if (error)
        error->clear();
    if (!validStore(settingsFilePath, accountId, error))
        return false;
    const QString canonical = canonicalGroupName(groupName);
    if (canonical.isEmpty()) {
        setError(error, QStringLiteral("invalid ANSRVR group name"));
        return false;
    }
    QStringList groups = readGroups(settingsFilePath, accountId);
    if (!groups.removeAll(canonical))
        return true;
    return writeGroups(settingsFilePath, accountId, groups, error);
}
} // namespace AprsRoom
