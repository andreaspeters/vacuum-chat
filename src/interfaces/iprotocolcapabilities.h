#ifndef IPROTOCOLCAPABILITIES_H
#define IPROTOCOLCAPABILITIES_H

#include <QFlags>
#include <QtPlugin>
#include <interfaces/identity.h>

class IProtocolCapabilities
{
public:
    enum Capability
    {
        CapabilityNone = 0x0,
        CapabilityAddContact = 1 << 0,
        CapabilitySetPresence = 1 << 1,
        CapabilityQuerySoftwareVersion = 1 << 2,
        CapabilitySetAccountAvatar = 1 << 3,
        CapabilityCopyIdentifier = 1 << 4,
        CapabilityViewHistory = 1 << 5,
        CapabilityManageRemoteArchive = 1 << 6,
        CapabilityShowProfile = 1 << 7,
        CapabilityEditProfile = 1 << 8
    };
    Q_DECLARE_FLAGS(Capabilities, Capability)

    virtual ~IProtocolCapabilities() {}

    // Report operations usable for this exact account and optional target now.
    // Adapters may include connection/session readiness in the returned flags.
    virtual Capabilities capabilitiesForAccount(const AccountId &accountId,
        const ConversationId &targetId = ConversationId()) const
    {
        Q_UNUSED(accountId);
        Q_UNUSED(targetId);
        return Capabilities();
    }

    // A non-empty request succeeds only when every requested capability exists.
    bool hasCapabilities(const AccountId &accountId, Capabilities requested,
        const ConversationId &targetId = ConversationId()) const
    {
        return requested != Capabilities() &&
            (capabilitiesForAccount(accountId, targetId) & requested) == requested;
    }
};

Q_DECLARE_OPERATORS_FOR_FLAGS(IProtocolCapabilities::Capabilities)
Q_DECLARE_INTERFACE(IProtocolCapabilities, "Vacuum.Plugin.IProtocolCapabilities/1.0")

#endif // IPROTOCOLCAPABILITIES_H