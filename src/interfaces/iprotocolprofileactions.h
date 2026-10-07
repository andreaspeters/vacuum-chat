#ifndef IPROTOCOLPROFILEACTIONS_H
#define IPROTOCOLPROFILEACTIONS_H

#include <QtPlugin>
#include <interfaces/identity.h>

struct IProtocolProfileActions
{
    virtual ~IProtocolProfileActions() {}

    virtual bool showProfile(const AccountId &accountId, const UserId &userId) = 0;
    virtual bool editProfile(const AccountId &accountId) = 0;
};

Q_DECLARE_INTERFACE(IProtocolProfileActions, "Vacuum.Plugin.IProtocolProfileActions/1.0")

#endif // IPROTOCOLPROFILEACTIONS_H
