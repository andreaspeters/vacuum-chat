#include "matrixhistorybackup.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

#include <algorithm>

namespace
{
QJsonObject eventToJson(const MatrixTextEvent &event)
{
    QJsonObject object;
    object.insert(QStringLiteral("event_id"), event.eventId);
    object.insert(QStringLiteral("room_id"), event.roomId);
    object.insert(QStringLiteral("user_id"), event.userId);
    object.insert(QStringLiteral("content"), event.content);
    object.insert(QStringLiteral("timestamp"), event.timestamp);
    object.insert(QStringLiteral("event_type"), event.eventType);
    object.insert(QStringLiteral("message_type"), event.messageType);
    object.insert(QStringLiteral("attachments"), QJsonArray::fromStringList(event.attachments));
    object.insert(QStringLiteral("metadata"), QJsonObject::fromVariantMap(event.metadata));
    return object;
}

bool eventFromJson(const QJsonObject &object, const QString &roomId, MatrixTextEvent &event)
{
    event.eventId = object.value(QStringLiteral("event_id")).toString();
    event.roomId = object.value(QStringLiteral("room_id")).toString();
    if (event.eventId.isEmpty() || event.roomId != roomId)
        return false;

    event.userId = object.value(QStringLiteral("user_id")).toString();
    event.content = object.value(QStringLiteral("content")).toString();
    event.timestamp = object.value(QStringLiteral("timestamp")).toString();
    event.eventType = object.value(QStringLiteral("event_type")).toString();
    event.messageType = object.value(QStringLiteral("message_type")).toString();
    for (const QJsonValue &value : object.value(QStringLiteral("attachments")).toArray())
        if (value.isString())
            event.attachments.append(value.toString());
    event.metadata = object.value(QStringLiteral("metadata")).toObject().toVariantMap();
    return true;
}

bool chronologicalLess(const MatrixTextEvent &left, const MatrixTextEvent &right)
{
    bool leftOk = false;
    bool rightOk = false;
    const qint64 leftTimestamp = left.timestamp.toLongLong(&leftOk);
    const qint64 rightTimestamp = right.timestamp.toLongLong(&rightOk);
    if (leftOk != rightOk)
        return leftOk;
    if (leftOk && leftTimestamp != rightTimestamp)
        return leftTimestamp < rightTimestamp;
    return left.eventId < right.eventId;
}
}

MatrixHistoryBackup::MatrixHistoryBackup(const QString &profileDirectory,
    const QString &normalizedServerUrl, const QString &userId)
    : FProfileDirectory(profileDirectory), FNormalizedServerUrl(normalizedServerUrl), FUserId(userId)
{
}

bool MatrixHistoryBackup::saveMessageHistory(const QString &roomId,
    const QList<MatrixTextEvent> &history) const
{
    if (FProfileDirectory.isEmpty() || FNormalizedServerUrl.isEmpty() ||
        FUserId.isEmpty() || roomId.isEmpty() || history.isEmpty())
        return false;

    QList<MatrixTextEvent> combined = history;
    QList<MatrixTextEvent> existing;
    if (loadMessageHistory(roomId, existing))
        combined.append(existing);

    QList<MatrixTextEvent> sorted;
    QSet<QString> seenEventIds;
    sorted.reserve(combined.size());
    for (const MatrixTextEvent &event : combined) {
        if (event.roomId != roomId || event.eventId.isEmpty() || seenEventIds.contains(event.eventId))
            continue;
        seenEventIds.insert(event.eventId);
        sorted.append(event);
    }
    std::stable_sort(sorted.begin(), sorted.end(), chronologicalLess);

    QJsonArray events;
    for (const MatrixTextEvent &event : sorted)
        events.append(eventToJson(event));

    QJsonObject root;
    root.insert(QStringLiteral("format_version"), 1);
    root.insert(QStringLiteral("room_id"), roomId);
    root.insert(QStringLiteral("events"), events);

    const QString path = backupFilePath(roomId);
    const QFileInfo fileInfo(path);
    QDir directory = fileInfo.dir();
    if (!directory.mkpath(QStringLiteral(".")) ||
        !QFile::setPermissions(directory.absolutePath(),
            QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner))
        return false;

    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (file.write(json) != json.size() ||
        !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
        !file.commit()) {
        file.cancelWriting();
        return false;
    }
    return QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

bool MatrixHistoryBackup::loadMessageHistory(const QString &roomId,
    QList<MatrixTextEvent> &history) const
{
    history.clear();
    if (FProfileDirectory.isEmpty() || FNormalizedServerUrl.isEmpty() ||
        FUserId.isEmpty() || roomId.isEmpty())
        return false;

    QFile file(backupFilePath(roomId));
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject())
        return false;
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format_version")).toInt() != 1 ||
        root.value(QStringLiteral("room_id")).toString() != roomId ||
        !root.value(QStringLiteral("events")).isArray())
        return false;

    QSet<QString> seenEventIds;
    for (const QJsonValue &value : root.value(QStringLiteral("events")).toArray()) {
        if (!value.isObject())
            continue;
        MatrixTextEvent event;
        if (eventFromJson(value.toObject(), roomId, event) && !seenEventIds.contains(event.eventId)) {
            seenEventIds.insert(event.eventId);
            history.append(event);
        }
    }
    std::stable_sort(history.begin(), history.end(), chronologicalLess);
    return true;
}

QString MatrixHistoryBackup::backupFilePath(const QString &roomId) const
{
    if (FProfileDirectory.isEmpty() || FNormalizedServerUrl.isEmpty() || FUserId.isEmpty() || roomId.isEmpty())
        return QString();
    const QString directory = QDir(FProfileDirectory).filePath(
        QStringLiteral("matrix-history-backup/%1/%2")
            .arg(hashString(FNormalizedServerUrl), hashString(FUserId)));
    return QDir(directory).filePath(hashString(roomId) + QStringLiteral(".json"));
}

QString MatrixHistoryBackup::hashString(const QString &value) const
{
    return QString::fromLatin1(QCryptographicHash::hash(value.toUtf8(),
        QCryptographicHash::Sha256).toHex());
}
