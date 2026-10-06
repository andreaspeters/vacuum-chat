#ifndef IPROTOCOLACCOUNT_H
#define IPROTOCOLACCOUNT_H

#include <QFlags>
#include <QObject>
#include <QString>
#include <QUuid>
#include <utils/options.h>

class IProtocolAccount
{
public:
	enum ProtocolKind
	{
		ProtocolUnknown,
		ProtocolXmpp,
		ProtocolMatrix,
		ProtocolMeshCore
	};

	static ProtocolKind protocolKindForType(const QString &type)
	{
		if (type.isEmpty() || type.compare(QStringLiteral("jabber"), Qt::CaseInsensitive) == 0 ||
			type.compare(QStringLiteral("xmpp"), Qt::CaseInsensitive) == 0)
			return ProtocolXmpp;
		if (type.compare(QStringLiteral("matrix"), Qt::CaseInsensitive) == 0)
			return ProtocolMatrix;
		if (type.compare(QStringLiteral("meshcore"), Qt::CaseInsensitive) == 0)
			return ProtocolMeshCore;
		return ProtocolUnknown;
	}

	enum Capability
	{
		CapabilityNone = 0x0,
		CapabilityChat = 0x1,
		CapabilityEncryption = 0x2,
		CapabilityConference = 0x4,
		CapabilityFileTransfer = 0x8
	};
	Q_DECLARE_FLAGS(Capabilities, Capability)

	enum ConnectionState
	{
		StateDisconnected,
		StateConnecting,
		StateConnected
	};

	virtual ~IProtocolAccount() {}
	virtual QObject *instance() = 0;
	virtual ProtocolKind protocolKind() const = 0;
	virtual Capabilities capabilities() const = 0;
	virtual ConnectionState connectionState() const = 0;
	virtual QUuid accountId() const = 0;
	virtual bool isActive() const = 0;
	virtual QString name() const = 0;
	virtual OptionsNode optionsNode() const = 0;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(IProtocolAccount::Capabilities)
Q_DECLARE_INTERFACE(IProtocolAccount, "Vacuum.Plugin.IProtocolAccount/1.0")

#endif // IPROTOCOLACCOUNT_H
