#ifndef IPROTOCOLSTATUSICONS_H
#define IPROTOCOLSTATUSICONS_H

#include <QObject>
#include <QIcon>
#include <QString>
#include <interfaces/identity.h>

class IProtocolStatusIcons
{
public:
    virtual ~IProtocolStatusIcons() {}
    virtual QObject *instance() = 0;
    virtual QIcon iconByIdentity(const AccountId &accountId, const UserId &userId) const = 0;
    virtual QString iconKeyByIdentity(const AccountId &accountId, const UserId &userId) const = 0;
};

Q_DECLARE_INTERFACE(IProtocolStatusIcons, "Vacuum.Plugin.IProtocolStatusIcons/1.0")

#endif // IPROTOCOLSTATUSICONS_H
