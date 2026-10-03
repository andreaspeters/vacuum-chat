#ifndef AUTOSTATUSPROTOCOLPRESENCE_H
#define AUTOSTATUSPROTOCOLPRESENCE_H

#include <QMap>
#include <QString>

#include <interfaces/iprotocolpresence.h>

namespace AutoStatusInternal {

class ProtocolPresenceController
{
public:
    bool apply(IProtocolPresence *presence, const QString &providerKey,
               bool eligibleForInitialChange, int targetShow, const QString &targetStatus)
    {
        if (!presence || providerKey.isEmpty() || presence->streamId().isEmpty())
            return false;

        const int currentShow = presence->show();
        const QString currentStatus = presence->status();
        const bool alreadyManaged = FPrevious.contains(providerKey);
        if (!alreadyManaged && !eligibleForInitialChange)
            return false;
        if (currentShow == targetShow && currentStatus == targetStatus)
            return true;

        if (!alreadyManaged)
            FPrevious.insert(providerKey, qMakePair(currentShow, currentStatus));
        if (!presence->setPresence(targetShow, targetStatus)) {
            if (!alreadyManaged)
                FPrevious.remove(providerKey);
            return false;
        }
        return true;
    }

    bool restore(IProtocolPresence *presence, const QString &providerKey)
    {
        if (!presence || providerKey.isEmpty() || presence->streamId().isEmpty() ||
            !FPrevious.contains(providerKey))
            return false;

        const QPair<int, QString> previous = FPrevious.value(providerKey);
        if (!presence->setPresence(previous.first, previous.second))
            return false;
        FPrevious.remove(providerKey);
        return true;
    }

private:
    QMap<QString, QPair<int, QString> > FPrevious;
};

} // namespace AutoStatusInternal

#endif // AUTOSTATUSPROTOCOLPRESENCE_H
