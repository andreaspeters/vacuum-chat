#ifndef IPROTOCOLADVERTACTIONS_H
#define IPROTOCOLADVERTACTIONS_H

#include <QtPlugin>
#include <interfaces/identity.h>

class IProtocolAdvertActions
{
public:
    enum class AdvertType { ZeroHop, Flood };

    virtual ~IProtocolAdvertActions() {}

    virtual bool sendSelfAdvert(const AccountId &accountId, AdvertType type)
    {
        Q_UNUSED(accountId);
        Q_UNUSED(type);
        return false;
    }
};

Q_DECLARE_INTERFACE(IProtocolAdvertActions,
    "Vacuum.Plugin.IProtocolAdvertActions/1.0")

#endif // IPROTOCOLADVERTACTIONS_H
