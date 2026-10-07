#ifndef IPROTOCOLACCOUNTAVATARACTIONS_H
#define IPROTOCOLACCOUNTAVATARACTIONS_H

#include <QByteArray>
#include <QtPlugin>
#include <interfaces/identity.h>

class IProtocolAccountAvatarActions
{
public:
    virtual ~IProtocolAccountAvatarActions() {}

    // Starts the protocol-specific update asynchronously. Empty data clears the avatar.
    virtual bool setAccountAvatar(const AccountId &accountId, const QByteArray &imageData) = 0;
};

Q_DECLARE_INTERFACE(IProtocolAccountAvatarActions,
    "Vacuum.Plugin.IProtocolAccountAvatarActions/1.0")

#endif // IPROTOCOLACCOUNTAVATARACTIONS_H
