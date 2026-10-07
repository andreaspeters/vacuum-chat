#ifndef IPROTOCOLCONTACTACTIONS_H
#define IPROTOCOLCONTACTACTIONS_H

#include <QtPlugin>
#include <interfaces/identity.h>

class IProtocolContactActions
{
public:
    virtual ~IProtocolContactActions() {}

    // Show the protocol's Add Contact form for this persistent account identity.
    virtual bool showAddContactDialog(const AccountId &accountId) = 0;
};

Q_DECLARE_INTERFACE(IProtocolContactActions, "Vacuum.Plugin.IProtocolContactActions/1.0")

#endif // IPROTOCOLCONTACTACTIONS_H
