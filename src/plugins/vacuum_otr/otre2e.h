#pragma once

/*
It's dirty port of psi OTR plugin to vacuum IM performed by Naoji Minami
*/

#define OTRE2E_UUID QUuid("{8592e3c3-ef5e-42a9-91c9-faf1ed9a91c5}")

#include <QMultiMap>

#include <definitions/toolbargroups.h>

#include <interfaces/ipluginmanager.h>
#include <interfaces/istanzaprocessor.h>
#include <interfaces/ixmppstreams.h>
#include <interfaces/ioptionsmanager.h>
#include <interfaces/imessagearchiver.h>
#include <utils/message.h>

#include "OtrMessaging.hpp"
#include "PsiOtrConfig.hpp"
#include "optionswidget.h"

#include "i_otr_datasource.h"

#include "stanza_catchers.h"

class QToolButton;
class QAction;

class IAccountManager;
class IMessageProcessor;
class IMessageWidgets;
class IToolBarWidget;
class Action;
class IMessageWindow;
class IChatWindow;

// Still not implemented:
// - Partner validation
// - Online/offline events support
// Bugs:
// - Apply button in settings dialog doesn't work

#define OPN_MISC_OTR                        "OTRPAGE"
#define OWO_MISC_OTR                        656

class OtrE2E
	: public QObject,
	public IPlugin,
	public IOptionsHolder,
	public IArchiveHandler,
	public IOTRDataSource
{
	Q_OBJECT;
	Q_PLUGIN_METADATA(IID "org.vacuum-im.vacuum_otr")

	Q_INTERFACES(IPlugin IOptionsHolder IArchiveHandler);
public:
	OtrE2E();
	~OtrE2E();
	//IPlugin
	virtual QObject *instance();
	virtual QUuid pluginUuid() const;
	virtual void pluginInfo(IPluginInfo *APluginInfo);
	virtual bool initConnections(IPluginManager *APluginManager, int &AInitOrder);
	virtual bool initObjects();
	virtual bool initSettings();
	virtual bool startPlugin();

	virtual void savePolicy(otr::OtrPolicy policy);
	virtual otr::OtrPolicy policy() const;

	virtual QMultiMap<int, IOptionsWidget *> optionsWidgets(const QString &ANodeId, QWidget *AParent);

	//qDebug(qPrintable(QString("%1->%2: %3").arg(message.from()).arg(message.to()).arg(message.body())));

	//IArchiveHandler
	virtual bool archiveMessageEdit(int AOrder, const Jid &AStreamJid, Message &AMessage, bool ADirectionIn);
public:
	virtual QString dataDir() const;
	virtual bool contactOnline(const QString& account, const QString& contact);
	// TODO: online/offline events
	virtual void sendMessage(const QString& account, const QString& to, const QString& messagetxt);
	virtual void fakeMessage(const QString& account, const QString& from, const QString& message);

	virtual void goneSecure(const QString& account, const QString& contact);
	virtual void goneInsecure(const QString& account, const QString& contact);

	virtual void updateContexts();
private:
	void insertToolBarAction(IToolBarWidget *AWidget);
	QString stateToString(otr::OtrMessageState state);

private slots:
	void onStreamOpened(IXmppStream *AXmppStream);
	void onStreamClosed(IXmppStream *AXmppStream);
	void onToolBarWidgetCreated(IToolBarWidget *AWidget);

	void onMessageWindowCreated(IMessageWindow *AWindow);
	void onMessageWindowDestroyed(IMessageWindow *AWindow);
	void onChatWindowCreated(IChatWindow *AWindow);
	void onChatWindowDestroyed(IChatWindow *AWindow);

	void onOtrMenuclicked(bool);
	void onMenuActionTriggered(QAction*);

private:
	otr::OtrMessaging* m_otr;
	IOptionsManager* m_optman;
	IStanzaProcessor *FStanzaProcessor;
	IMessageArchiver *FMessageArchiver;
	IAccountManager* m_accountManager;
	IMessageProcessor* m_processor;
	IMessageWidgets* m_msgWidgets;
	InboundStanzaCatcher* m_inboundCatcher;
	OutboundStanzaCatcher* m_outboundCatcher;
	QString m_homePath;
	QHash<IToolBarWidget*, Action*> m_actions;
	QHash<Action*, QToolButton*> m_buttons;
};

class OtrSessPair
{
public:
	enum ActType
	{
		Refresh = 0,
		Stop
	};

	OtrSessPair()
	{}

	OtrSessPair(const QString& localid, const QString& partnerid, ActType atype)
		: m_localId(localid),
		m_partnerId(partnerid),
		m_type(atype)
	{

	}
	QString localId() const
	{
		return m_localId;
	}
	QString partnerId() const
	{
		return m_partnerId;
	}
	ActType actType()
	{
		return m_type;
	}
private:
	QString m_localId;
	QString m_partnerId;
	ActType m_type;
};

Q_DECLARE_METATYPE(OtrSessPair);

