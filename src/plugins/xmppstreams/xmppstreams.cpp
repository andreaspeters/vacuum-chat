#include "xmppstreams.h"
#include <interfaces/iaccountmanager.h>
#include <interfaces/iroster.h>
#include <interfaces/irosterchanger.h>
#include <interfaces/iavatars.h>
#include <interfaces/imessagearchiver.h>
#include <interfaces/ivcard.h>
#include "xmppprofileactionpolicy.h"

namespace
{
IAccount *configuredXmppAccount(IPluginManager *pluginManager, const AccountId &accountId)
{
	if (!pluginManager || accountId.isEmpty())
		return NULL;
	const QUuid persistentAccountId = QUuid::fromString(accountId);
	if (persistentAccountId.isNull())
		return NULL;
	IPlugin *plugin = pluginManager->pluginInterface("IAccountManager").value(0, NULL);
	IAccountManager *accountManager = plugin
		? qobject_cast<IAccountManager *>(plugin->instance()) : NULL;
	IAccount *account = accountManager ? accountManager->accountById(persistentAccountId) : NULL;
	return account && account->accountId() == persistentAccountId && account->isActive() &&
		account->protocolKind() == IProtocolAccount::ProtocolXmpp ? account : NULL;
}

IAccount *connectedXmppAccount(IPluginManager *pluginManager, const AccountId &accountId)
{
	IAccount *account = configuredXmppAccount(pluginManager, accountId);
	return account && account->connectionState() == IProtocolAccount::StateConnected ? account : NULL;
}

IVCardPlugin *vcardPlugin(IPluginManager *pluginManager)
{
	IPlugin *plugin = pluginManager ? pluginManager->pluginInterface("IVCardPlugin").value(0, NULL) : NULL;
	return plugin ? qobject_cast<IVCardPlugin *>(plugin->instance()) : NULL;
}

IRoster *openXmppRoster(IPluginManager *pluginManager, IAccount *account)
{
	if (!pluginManager || !account)
		return NULL;
	IPlugin *plugin = pluginManager->pluginInterface("IRosterPlugin").value(0, NULL);
	IRosterPlugin *rosterPlugin = plugin
		? qobject_cast<IRosterPlugin *>(plugin->instance()) : NULL;
	IRoster *roster = rosterPlugin ? rosterPlugin->findRoster(account->streamJid()) : NULL;
	return roster && roster->isOpen() ? roster : NULL;
}
}

XmppStreams::XmppStreams()
{
	FPluginManager = NULL;
}

XmppStreams::~XmppStreams()
{

}

void XmppStreams::pluginInfo(IPluginInfo *APluginInfo)
{
	APluginInfo->name = tr("XMPP Streams Manager");
	APluginInfo->description = tr("Allows other modules to create XMPP streams and get access to them");
	APluginInfo ->version = "1.0";
	APluginInfo->author = "Potapov S.A. aka Lion";
	APluginInfo->homePage = "https://github.com/andreaspeters/vacuum-chat";
}

bool XmppStreams::initConnections(IPluginManager *APluginManager, int &AInitOrder)
{
	FPluginManager = APluginManager;
	Q_UNUSED(AInitOrder);
	return true;
}

IProtocolCapabilities::Capabilities XmppStreams::capabilitiesForAccount(
	const AccountId &accountId, const ConversationId &targetId) const
{
	IAccount *account = connectedXmppAccount(FPluginManager, accountId);
	IProtocolCapabilities::Capabilities capabilities;
	if (openXmppRoster(FPluginManager, account))
		capabilities |= IProtocolCapabilities::CapabilityAddContact;
	if (account && Jid(targetId).isValid())
		capabilities |= IProtocolCapabilities::CapabilityQuerySoftwareVersion;
	IPlugin *archivePlugin = FPluginManager
		? FPluginManager->pluginInterface("IMessageArchiver").value(0, NULL) : NULL;
	IMessageArchiver *messageArchiver = archivePlugin
		? qobject_cast<IMessageArchiver *>(archivePlugin->instance()) : NULL;
	if (account && messageArchiver && messageArchiver->isReady(account->streamJid()) &&
		(messageArchiver->totalCapabilities(account->streamJid()) & IArchiveEngine::ArchiveManagement))
		capabilities |= IProtocolCapabilities::CapabilityManageRemoteArchive;
	IPlugin *avatarPlugin = FPluginManager
		? FPluginManager->pluginInterface("IAvatars").value(0, NULL) : NULL;
	IAvatars *avatars = avatarPlugin
		? qobject_cast<IAvatars *>(avatarPlugin->instance()) : NULL;
	if (account && avatars)
		capabilities |= IProtocolCapabilities::CapabilitySetAccountAvatar;
	IAccount *profileAccount = configuredXmppAccount(FPluginManager, accountId);
	IVCardPlugin *vcard = vcardPlugin(FPluginManager);
	const Jid targetJid(targetId);
	capabilities |= XmppProfileActionPolicy::capabilities(account != NULL,
		profileAccount != NULL && vcard != NULL, targetJid.isValid(),
		targetJid.isValid() && vcard && vcard->hasVCard(targetJid.bare()));
	return capabilities;
}

bool XmppStreams::showAddContactDialog(const AccountId &accountId)
{
	if (!hasCapabilities(accountId, IProtocolCapabilities::CapabilityAddContact))
		return false;
	IAccount *account = connectedXmppAccount(FPluginManager, accountId);
	if (!openXmppRoster(FPluginManager, account))
		return false;
	IPlugin *plugin = FPluginManager->pluginInterface("IRosterChanger").value(0, NULL);
	IRosterChanger *rosterChanger = plugin
		? qobject_cast<IRosterChanger *>(plugin->instance()) : NULL;
	return rosterChanger && rosterChanger->showAddContactDialog(account->streamJid()) != NULL;
}

bool XmppStreams::showProfile(const AccountId &accountId, const UserId &userId)
{
	if (!hasCapabilities(accountId, IProtocolCapabilities::CapabilityShowProfile, userId))
		return false;
	IAccount *account = configuredXmppAccount(FPluginManager, accountId);
	IVCardPlugin *vcard = vcardPlugin(FPluginManager);
	const Jid targetJid(userId);
	if (!account || !vcard || !targetJid.isValid())
		return false;
	const bool sessionReady = connectedXmppAccount(FPluginManager, accountId) != NULL;
	if (!sessionReady && !vcard->hasVCard(targetJid.bare()))
		return false;
	return XmppProfileActionPolicy::showProfile(vcard, account->streamJid(), userId);
}

bool XmppStreams::editProfile(const AccountId &accountId)
{
	if (!hasCapabilities(accountId, IProtocolCapabilities::CapabilityEditProfile))
		return false;
	IAccount *account = connectedXmppAccount(FPluginManager, accountId);
	IVCardPlugin *vcard = vcardPlugin(FPluginManager);
	return account && vcard && XmppProfileActionPolicy::editProfile(vcard, account->streamJid());
}

bool XmppStreams::setAccountAvatar(const AccountId &accountId, const QByteArray &imageData)
{
	if (!hasCapabilities(accountId, IProtocolCapabilities::CapabilitySetAccountAvatar))
		return false;
	IAccount *account = connectedXmppAccount(FPluginManager, accountId);
	IPlugin *avatarPlugin = FPluginManager
		? FPluginManager->pluginInterface("IAvatars").value(0, NULL) : NULL;
	IAvatars *avatars = avatarPlugin
		? qobject_cast<IAvatars *>(avatarPlugin->instance()) : NULL;
	return account && avatars && avatars->setAvatar(account->streamJid(), imageData);
}

bool XmppStreams::initObjects()
{
	XmppError::registerError(NS_INTERNAL_ERROR,IERR_XMPPSTREAM_DESTROYED,tr("XMPP stream destroyed"));
	XmppError::registerError(NS_INTERNAL_ERROR,IERR_XMPPSTREAM_NOT_SECURE,tr("Secure connection is not established"));
	XmppError::registerError(NS_INTERNAL_ERROR,IERR_XMPPSTREAM_CLOSED_UNEXPECTEDLY,tr("Connection closed unexpectedly"));
	XmppError::registerError(NS_INTERNAL_ERROR,IERR_XMPPSTREAM_FAILED_START_CONNECTION,tr("Failed to start connection"));
	return true;
}

bool XmppStreams::initSettings()
{
	Options::setDefaultValue(OPV_XMPPSTREAMS_TIMEOUT_HANDSHAKE,60000);
	Options::setDefaultValue(OPV_XMPPSTREAMS_TIMEOUT_KEEPALIVE,30000);
	Options::setDefaultValue(OPV_XMPPSTREAMS_TIMEOUT_DISCONNECT,5000);
	return true;
}

QList<IXmppStream *> XmppStreams::xmppStreams() const
{
	return FStreams;
}

IXmppStream *XmppStreams::xmppStream(const Jid &AStreamJid) const
{
	foreach(IXmppStream *stream,FStreams)
		if (stream->streamJid() == AStreamJid)
			return stream;
	return NULL;
}

IXmppStream *XmppStreams::newXmppStream(const Jid &AStreamJid)
{
	IXmppStream *stream = xmppStream(AStreamJid);
	if (!stream)
	{
		stream = new XmppStream(this, AStreamJid);
		connect(stream->instance(), SIGNAL(streamDestroyed()), SLOT(onStreamDestroyed()));
		FStreams.append(stream);
		emit created(stream);
	}
	return stream;
}

bool XmppStreams::isActive( IXmppStream *AXmppStream ) const
{
	return FActiveStreams.contains(AXmppStream);
}

void XmppStreams::addXmppStream(IXmppStream *AXmppStream)
{
	if (AXmppStream && !FActiveStreams.contains(AXmppStream))
	{
		connect(AXmppStream->instance(), SIGNAL(opened()), SLOT(onStreamOpened()));
		connect(AXmppStream->instance(), SIGNAL(aboutToClose()), SLOT(onStreamAboutToClose()));
		connect(AXmppStream->instance(), SIGNAL(closed()), SLOT(onStreamClosed()));
		connect(AXmppStream->instance(), SIGNAL(error(const XmppError &)), SLOT(onStreamError(const XmppError &)));
		connect(AXmppStream->instance(), SIGNAL(jidAboutToBeChanged(const Jid &)), SLOT(onStreamJidAboutToBeChanged(const Jid &)));
		connect(AXmppStream->instance(), SIGNAL(jidChanged(const Jid &)), SLOT(onStreamJidChanged(const Jid &)));
		connect(AXmppStream->instance(), SIGNAL(connectionChanged(IConnection *)), SLOT(onStreamConnectionChanged(IConnection *)));
		FActiveStreams.append(AXmppStream);
		emit added(AXmppStream);
	}
}

void XmppStreams::removeXmppStream(IXmppStream *AXmppStream)
{
	if (FActiveStreams.contains(AXmppStream))
	{
		if (AXmppStream->isConnected())
		{
			AXmppStream->close();
			AXmppStream->connection()->disconnectFromHost();
		}
		AXmppStream->instance()->disconnect(this);
		connect(AXmppStream->instance(), SIGNAL(streamDestroyed()),SLOT(onStreamDestroyed()));
		FActiveStreams.removeAt(FActiveStreams.indexOf(AXmppStream));
		emit removed(AXmppStream);
	}
}

void XmppStreams::destroyXmppStream(const Jid &AStreamJid)
{
	IXmppStream *stream = xmppStream(AStreamJid);
	if (stream)
		delete stream->instance();
}

QList<QString> XmppStreams::xmppFeatures() const
{
	return FFeatureOrders.values();
}

void XmppStreams::registerXmppFeature(int AOrder, const QString &AFeatureNS)
{
	if (!AFeatureNS.isEmpty() && !FFeatureOrders.values().contains(AFeatureNS))
	{
		FFeatureOrders.insertMulti(AOrder,AFeatureNS);
		emit xmppFeatureRegistered(AOrder,AFeatureNS);
	}
}

QList<IXmppFeaturesPlugin *> XmppStreams::xmppFeaturePlugins(const QString &AFeatureNS) const
{
	return FFeaturePlugins.value(AFeatureNS).values();
}

void XmppStreams::registerXmppFeaturePlugin(int AOrder, const QString &AFeatureNS, IXmppFeaturesPlugin *AFeaturePlugin)
{
	if (AFeaturePlugin && !AFeatureNS.isEmpty())
	{
		FFeaturePlugins[AFeatureNS].insertMulti(AOrder,AFeaturePlugin);
		emit xmppFeaturePluginRegistered(AOrder,AFeatureNS,AFeaturePlugin);
	}
}

void XmppStreams::onStreamOpened()
{
	IXmppStream *stream = qobject_cast<IXmppStream *>(sender());
	if (stream)
		emit opened(stream);
}

void XmppStreams::onStreamAboutToClose()
{
	IXmppStream *stream = qobject_cast<IXmppStream *>(sender());
	if (stream)
		emit aboutToClose(stream);
}
void XmppStreams::onStreamClosed()
{
	IXmppStream *stream = qobject_cast<IXmppStream *>(sender());
	if (stream)
		emit closed(stream);
}

void XmppStreams::onStreamError(const XmppError &AError)
{
	IXmppStream *stream = qobject_cast<IXmppStream *>(sender());
	if (stream)
		emit error(stream,AError);
}

void XmppStreams::onStreamJidAboutToBeChanged(const Jid &AAfter)
{
	IXmppStream *stream = qobject_cast<IXmppStream *>(sender());
	if (stream)
		emit jidAboutToBeChanged(stream,AAfter);
}

void XmppStreams::onStreamJidChanged(const Jid &ABefore)
{
	IXmppStream *stream = qobject_cast<IXmppStream *>(sender());
	if (stream)
		emit jidChanged(stream,ABefore);
}

void XmppStreams::onStreamConnectionChanged(IConnection *AConnection)
{
	IXmppStream *stream = qobject_cast<IXmppStream *>(sender());
	if (stream)
		emit connectionChanged(stream,AConnection);
}

void XmppStreams::onStreamDestroyed()
{
	IXmppStream *stream = qobject_cast<IXmppStream *>(sender());
	if (stream)
	{
		removeXmppStream(stream);
		FStreams.removeAt(FStreams.indexOf(stream));
		emit streamDestroyed(stream);
	}
}


