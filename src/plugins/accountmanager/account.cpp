#include "account.h"

#include <QUrl>

Account::Account(IXmppStreams *AXmppStreams, const OptionsNode &AOptionsNode, QObject *AParent) : QObject(AParent)
{
	FXmppStreams = AXmppStreams;
	FOptionsNode = AOptionsNode;
	FXmppStream = NULL;
	FActive = false;

	connect(Options::instance(),SIGNAL(optionsChanged(const OptionsNode &)),SLOT(onOptionsChanged(const OptionsNode &)));
}

Account::~Account()
{

}

bool Account::isValid() const
{
	const QString type = FOptionsNode.value("type").toString();
	const bool matrix = type.compare("matrix", Qt::CaseInsensitive) == 0;
	const bool meshcore = type.compare("meshcore", Qt::CaseInsensitive) == 0;

	if (matrix)
	{
		const QString instance = FOptionsNode.value("matrix.instance").toString().trimmed();
		const QString username = FOptionsNode.value("matrix.username").toString().trimmed();
		bool valid = !instance.isEmpty();
		valid = valid && !username.isEmpty();

		if (valid)
		{
			const QUrl url(instance);
			const QString scheme = url.scheme().toLower();
			valid = url.isValid() && (scheme == QStringLiteral("http") || scheme == QStringLiteral("https"))
					&& !url.host().isEmpty();
		}

		return valid;
	}
	else if (meshcore)
	{
		const QString transport = FOptionsNode.value("meshcore.transport").toString().trimmed().toLower();
		if (transport != QStringLiteral("ble") && transport != QStringLiteral("usb"))
			return false;
		if (transport == QStringLiteral("ble"))
			return !FOptionsNode.value("meshcore.mac").toString().trimmed().isEmpty();
		return !FOptionsNode.value("meshcore.port").toString().trimmed().isEmpty();
	}
	else
	{
		Jid sJid = streamJid();
		bool valid = sJid.isValid();
		valid = valid && !sJid.node().isEmpty();
		valid = valid && !sJid.domain().isEmpty();
		valid = valid && (FXmppStream==FXmppStreams->xmppStream(sJid) || FXmppStreams->xmppStream(sJid)==NULL);
		return valid;
	}
}

QUuid Account::accountId() const
{
	return QUuid::fromString(FOptionsNode.nspace());
}

bool Account::isActive() const
{
	return FActive;
}

void Account::setActive(bool AActive)
{
	const bool matrix = protocolKind() == ProtocolMatrix;
	const bool meshcore = protocolKind() == ProtocolMeshCore;

	if (matrix || meshcore)
	{
		if (AActive && !FActive && isValid())
		{
			FActive = true;
			emit activeChanged(true);
		}
		else if (!AActive && FActive)
		{
			FActive = false;
			emit activeChanged(false);
		}
	}
	else
	{
		if (AActive && FXmppStream==NULL && isValid())
		{
			FXmppStream = FXmppStreams->newXmppStream(streamJid());
			connect(FXmppStream->instance(),SIGNAL(closed()),SLOT(onXmppStreamClosed()),Qt::QueuedConnection);
			onXmppStreamClosed();
			FXmppStreams->addXmppStream(FXmppStream);
			FActive = true;
			emit activeChanged(true);
		}
		else if (!AActive && FXmppStream!=NULL)
		{
			FActive = false;
			emit activeChanged(false);
			FXmppStreams->removeXmppStream(FXmppStream);
			FXmppStreams->destroyXmppStream(FXmppStream->streamJid());
			FXmppStream = NULL;
		}
	}
}

QString Account::name() const
{
	return FOptionsNode.value("name").toString();
}

void Account::setName(const QString &AName)
{
	FOptionsNode.setValue(AName,"name");
}

Jid Account::streamJid() const
{
	return FOptionsNode.value("streamJid").toString();
}

void Account::setStreamJid(const Jid &AJid)
{
	FOptionsNode.setValue(AJid.full(),"streamJid");
}

QString Account::password() const
{
	if (protocolKind() == ProtocolMatrix)
	{
		const QByteArray matrixPassword = FOptionsNode.value("matrix.password").toByteArray();
		if (!matrixPassword.isEmpty())
			return Options::decrypt(matrixPassword).toString();
	}
	return Options::decrypt(FOptionsNode.value("password").toByteArray()).toString();
}

void Account::setPassword(const QString &APassword)
{
	FOptionsNode.setValue(Options::encrypt(APassword),
		protocolKind() == ProtocolMatrix ? "matrix.password" : "password");
}

OptionsNode Account::optionsNode() const
{
	return FOptionsNode;
}

IXmppStream *Account::xmppStream() const
{
	return FXmppStream;
}

IProtocolAccount::ProtocolKind Account::protocolKind() const
{
	return protocolKindForType(FOptionsNode.value("type").toString());
}

IProtocolAccount::Capabilities Account::capabilities() const
{
	Capabilities result = CapabilityChat;
	if (FOptionsNode.node("require-encryption").value().toBool())
		result |= CapabilityEncryption;
	return result;
}

IProtocolAccount::ConnectionState Account::connectionState() const
{
	return FXmppStream && FXmppStream->isConnected() ? StateConnected : StateDisconnected;
}

void Account::onXmppStreamClosed()
{
	if (FXmppStream)
	{
		FXmppStream->setStreamJid(streamJid());
		FXmppStream->setPassword(password());
		FXmppStream->setEncryptionRequired(FOptionsNode.node("require-encryption").value().toBool());
	}
}

void Account::onOptionsChanged(const OptionsNode &ANode)
{
	if (FOptionsNode.isChildNode(ANode))
	{
		if (FXmppStream && !FXmppStream->isConnected())
		{
			if (FOptionsNode.node("streamJid") == ANode)
			{
				FXmppStream->setStreamJid(ANode.value().toString());
			}
			else if (FOptionsNode.node("password") == ANode)
			{
				FXmppStream->setPassword(Options::decrypt(ANode.value().toByteArray()).toString());
			}
			else if (FOptionsNode.node("require-encryption") == ANode)
			{
				FXmppStream->setEncryptionRequired(ANode.value().toBool());
			}
		}
		emit optionsChanged(ANode);
	}
}