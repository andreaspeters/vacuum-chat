#include "otre2e.h"

#include <definitions/namespaces.h>
#include <definitions/archivehandlerorders.h>
#include <interfaces/iaccountmanager.h>
#include <interfaces/imessageprocessor.h>
#include <interfaces/imessagewidgets.h>

#include <QtCore/QPair>
#include <QtGui/QMenu>
#include <QtGui/QToolButton>

#define ADR_STREAM_JID            Action::DR_StreamJid
#define ADR_CONTACT_JID           Action::DR_Parametr1
#define ADR_GROUP                 Action::DR_Parametr3



	OtrE2E::OtrE2E()
	: m_otr(0),
	m_optman(0),
	m_inboundCatcher(0),
	m_outboundCatcher(0)
{

}

OtrE2E::~OtrE2E()
{
	delete m_otr;
}

void OtrE2E::pluginInfo(IPluginInfo *APluginInfo)
{
	APluginInfo->name = tr("OTR E2E");
	APluginInfo->description = tr("Off The Record messaging (OTR) ");
	APluginInfo->version = "0.1.1";
	APluginInfo->author = "Naoji Minami";
	APluginInfo->homePage = "http://code.google.com/p/vacuum-plugins";
	APluginInfo->dependences.append(MESSAGEPROCESSOR_UUID);
	APluginInfo->dependences.append(ACCOUNTMANAGER_UUID);
	APluginInfo->dependences.append(STANZAPROCESSOR_UUID);
	APluginInfo->dependences.append(XMPPSTREAMS_UUID);
}

bool OtrE2E::initConnections(IPluginManager *APluginManager, int &/*AInitOrder*/)
{
	m_homePath = APluginManager->homePath();
	//qDebug(qPrintable(QString("Home: %1").arg(m_homePath)));
	IPlugin *plugin = APluginManager->pluginInterface("IStanzaProcessor").value(0,NULL);
	if (plugin)
	{
		FStanzaProcessor = qobject_cast<IStanzaProcessor *>(plugin->instance());
	}
	plugin = APluginManager->pluginInterface("IXmppStreams").value(0,NULL);
	if (plugin)
	{
		IXmppStreams *FXmppStreams = qobject_cast<IXmppStreams *>(plugin->instance());
		if (FXmppStreams)
		{
			connect(FXmppStreams->instance(), SIGNAL(opened(IXmppStream *)), SLOT(onStreamOpened(IXmppStream *)));
			connect(FXmppStreams->instance(), SIGNAL(closed(IXmppStream *)), SLOT(onStreamClosed(IXmppStream *)));
		}
	}
	plugin = APluginManager->pluginInterface("IAccountManager").value(0,NULL);
	m_accountManager = qobject_cast<IAccountManager *>(plugin->instance());

	plugin = APluginManager->pluginInterface("IMessageProcessor").value(0,NULL);
	if (plugin)
		m_processor = qobject_cast<IMessageProcessor *>(plugin->instance());

	plugin = APluginManager->pluginInterface("IMessageWidgets").value(0,NULL);
	if (plugin)
	{
		m_msgWidgets = qobject_cast<IMessageWidgets *>(plugin->instance());
		if (m_msgWidgets)
		{
			connect(m_msgWidgets->instance(), SIGNAL(toolBarWidgetCreated(IToolBarWidget *)), SLOT(onToolBarWidgetCreated(IToolBarWidget *)));
			connect(m_msgWidgets->instance(), SIGNAL(messageWindowCreated(IMessageWindow *)), SLOT(onMessageWindowCreated(IMessageWindow *)));
			connect(m_msgWidgets->instance(), SIGNAL(messageWindowDestroyed(IMessageWindow *)), SLOT(onMessageWindowDestroyed(IMessageWindow *)));
			connect(m_msgWidgets->instance(), SIGNAL(chatWindowCreated(IChatWindow *)), SLOT(onChatWindowCreated(IChatWindow *)));
			connect(m_msgWidgets->instance(), SIGNAL(chatWindowDestroyed(IChatWindow *)), SLOT(onChatWindowDestroyed(IChatWindow *)));
		}
	}

	plugin = APluginManager->pluginInterface("IOptionsManager").value(0,NULL);
	if (plugin)
		m_optman = qobject_cast<IOptionsManager *>(plugin->instance());

	plugin = APluginManager->pluginInterface("IMessageArchiver").value(0);
	if (plugin)
	{
		FMessageArchiver = qobject_cast<IMessageArchiver *>(plugin->instance());
	}

	Q_ASSERT(m_accountManager);
	Q_ASSERT(m_processor);
	Q_ASSERT(m_optman);

	m_otr = new otr::OtrMessaging(this, otr::OTR_POLICY_ENABLED);
	m_inboundCatcher = new InboundStanzaCatcher(m_otr, m_accountManager, this);
	m_outboundCatcher = new OutboundStanzaCatcher(m_otr, m_accountManager, this);

	return (FStanzaProcessor != NULL);

	return true;
}

bool OtrE2E::initObjects()
{
	if (FMessageArchiver)
	{
		qDebug("archive processor registered");
		FMessageArchiver->insertArchiveHandler(AHO_DEFAULT,this);
	}

	IStanzaHandle handle_in;
	handle_in.handler = m_inboundCatcher;
	handle_in.order = -32767;
	handle_in.direction = IStanzaHandle::DirectionIn;
	//handle_in.streamJid = AXmppStream->streamJid();
	handle_in.conditions.append("/message");

	IStanzaHandle handle_out;
	handle_out.handler = m_outboundCatcher;
	handle_out.order = 32767;
	handle_out.direction = IStanzaHandle::DirectionOut;
	//handle_out.streamJid = AXmppStream->streamJid();
	handle_out.conditions.append("/message");

	FStanzaProcessor->insertStanzaHandle(handle_in);
	FStanzaProcessor->insertStanzaHandle(handle_out);


	return true;
}

QObject * OtrE2E::instance()
{
	return this;
}

QUuid OtrE2E::pluginUuid() const
{
	return OTRE2E_UUID;
}

bool OtrE2E::initSettings()
{
	Options::setDefaultValue(OPV_OTR_POLICY, otr::OTR_POLICY_ENABLED);
	if (m_optman)
	{
		IOptionsDialogNode dnode = { OWO_MISC_OTR, OPN_MISC_OTR, tr("OTR"), OPN_MISC_OTR };
		m_optman->insertOptionsDialogNode(dnode);
		m_optman->insertOptionsHolder(this);
	}
	return true;
}

bool OtrE2E::startPlugin()
{
	//qDebug(__PRETTY_FUNCTION__);
	return true;
}

QMultiMap<int, IOptionsWidget *> OtrE2E::optionsWidgets(const QString &ANodeId, QWidget *AParent)
{
	Q_UNUSED(AParent);
	QMultiMap<int, IOptionsWidget *> widgets;
	if (ANodeId == OPN_MISC_OTR)
	{
		QWidget* cfgd = new otr::ConfigDialog(m_otr, this);
		widgets.insertMulti(OWO_MISC_OTR, new OptionsWidget(cfgd, 0));
	}
	return widgets;
}
///////////////////////////////////////////////////////////////////////////

//IArchiveHandler
bool OtrE2E::archiveMessageEdit(int AOrder, const Jid &AStreamJid, Message &AMessage, bool ADirectionIn)
{
	Q_UNUSED(AOrder);
	Q_UNUSED(AStreamJid);
	Q_UNUSED(ADirectionIn);

	return AMessage.stanza().attribute(StanzaCatcher::SkipOtrCatcherFlag()) != "true";
}

void OtrE2E::savePolicy(otr::OtrPolicy policy)
{
	Options::node(OPV_OTR_POLICY).setValue(static_cast<int>(policy));
}

otr::OtrPolicy OtrE2E::policy() const
{
	QVariant val = Options::node(OPV_OTR_POLICY).value();
	return static_cast<otr::OtrPolicy>(val.value<int>());
}

QString OtrE2E::dataDir() const
{
	return m_homePath;
}

bool OtrE2E::contactOnline( const QString& account, const QString& contact )
{
	// TODO
	return true;
}

void OtrE2E::onStreamClosed( IXmppStream *AXmppStream )
{

}

void OtrE2E::sendMessage( const QString& account, const QString& to, const QString& messagetxt )
{
	//qDebug(__PRETTY_FUNCTION__);
	Message message;
	message.setType(Message::Chat);
	/*message.setThreadId(window->threadId());*/
	message.setBody(messagetxt);

	if (!message.body().isEmpty())
	{
		message.setTo(to);
		message.stanza().setAttribute(StanzaCatcher::SkipOtrCatcherFlag(), "true");
		m_processor->sendMessage(m_accountManager->accountById(account)->streamJid(), message, IMessageProcessor::MessageOut);
		//FStanzaProcessor->sendStanzaOut(m_accountManager->accountById(account)->streamJid(), message.stanza());
		//qDebug(qPrintable(QString(">>>>>>>>>>>>>>>>>>>>>>\n%1->%2: %3").arg(account).arg(to).arg(messagetxt)));
	}
}

void OtrE2E::fakeMessage(const QString& account, const QString& from, const QString& messagetext)
{
	/*qDebug(__PRETTY_FUNCTION__);*/
	/*qDebug(qPrintable(messagetext));*/
	Message message;
	message.setType(Message::Chat);
	message.setBody(messagetext);

	if (!message.body().isEmpty())
	{
		message.setFrom(from);
		message.setTo(m_accountManager->accountById(account)->streamJid().full());
		message.stanza().setAttribute(StanzaCatcher::SkipOtrCatcherFlag(), "true");

		m_processor->sendMessage(m_accountManager->accountById(account)->streamJid(), 
			message, IMessageProcessor::MessageIn);
		//m_processor->receiveMessage(message);
	}
}

void OtrE2E::onStreamOpened( IXmppStream *AXmppStream )
{
}

void OtrE2E::onToolBarWidgetCreated( IToolBarWidget * )
{
	//qDebug(__PRETTY_FUNCTION__);
	/**/
}

void OtrE2E::onMessageWindowCreated(IMessageWindow *)
{
	//qDebug(__PRETTY_FUNCTION__);
}

void  OtrE2E::onMessageWindowDestroyed(IMessageWindow *)
{
	//qDebug(__PRETTY_FUNCTION__);
}

void  OtrE2E::onChatWindowCreated(IChatWindow *AWindow)
{
	if (AWindow && AWindow->editWidget() && AWindow->toolBarWidget())
	{// skip history windows
		insertToolBarAction(AWindow->toolBarWidget());
		updateContexts();
	}
}

void  OtrE2E::onChatWindowDestroyed(IChatWindow *AWindow)
{
	//qDebug(__PRETTY_FUNCTION__);
	IToolBarWidget* tbw = AWindow->toolBarWidget();
	if (!tbw)
		return;
	if (m_actions.contains(tbw))
	{
		Action* act = m_actions.value(tbw);
		m_actions.remove(tbw);

		if (!act)
			return;
		if (m_buttons.contains(act))
		{
			m_buttons.remove(act);
		}
	}
}

void OtrE2E::insertToolBarAction( IToolBarWidget *AWidget )
{
	// TODO: XXX:
	if (m_actions.value(AWidget) == NULL)
	{
		Action *action = NULL;

		action = new Action(AWidget->toolBarChanger()->toolBar());
		//action->setIcon(RSR_STORAGE_MENUICONS, MNI_FILETRANSFER_SEND);
		action->setText(tr("OTR Status"));
		connect(action,SIGNAL(triggered(bool)),SLOT(onOtrMenuclicked(bool)));
		QToolButton* b = AWidget->toolBarChanger()->insertAction(action,TBG_MWTBW_CHATSTATES);
		m_actions.insert(AWidget, action);
		m_buttons.insert(action, b);
	}
}


void OtrE2E::onOtrMenuclicked( bool )
{
	Action *action = qobject_cast<Action *>(sender());
	if (action)
	{
		IToolBarWidget *widget = m_actions.key(action);
		if (!widget)
			return;

		IEditWidget* ew = widget->editWidget();
		if (ew)
		{
			QMenu* menu = new QMenu(0);
			connect(menu, SIGNAL(triggered(QAction *)),
				this, SLOT(onMenuActionTriggered(QAction*)));
			QAction* aRefresh = new QAction(tr("Refresh session"), menu);
			QAction* aStop = new QAction(tr("Stop session"), menu);
			QAction* aStatus = new QAction(menu);
			aStatus->setEnabled(false);

			menu->addAction(aStatus);
			menu->addSeparator();
			menu->addAction(aRefresh);
			menu->addAction(aStop);

			QString accId =m_accountManager->accountByStream(ew->streamJid())->accountId().toString();
			QString jid = ew->contactJid().full();
			//.bare();

			QVariant spstop;
			spstop.setValue(OtrSessPair(accId, jid, OtrSessPair::Stop));
			aStop->setData(spstop);

			QVariant spref;
			spref.setValue(OtrSessPair(accId, jid, OtrSessPair::Refresh));
			aRefresh->setData(spref);

			//QString sessid = m_otr->getSessionId(accId, jid);
			//qDebug(qPrintable(sessid));

			otr::OtrMessageState state = m_otr->getMessageState(accId, jid);
			QString otrstate = stateToString(state);
			aStatus->setText(QString("%1").arg(otrstate));

			QToolButton* b = m_buttons.value(action);
			Q_ASSERT(b);
			menu->popup(b->mapToGlobal(b->rect().bottomLeft()));
		}
	}
}

QString OtrE2E::stateToString(otr::OtrMessageState state)
{
	QString otrstate = QString("Bad state");

	if (state == otr::OTR_MESSAGESTATE_ENCRYPTED)
	{
		otrstate = tr("Encrypted");
	}
	else
		if (state == otr::OTR_MESSAGESTATE_PLAINTEXT)
		{
			otrstate = tr("Plaintext");
		}
		else
			if (state == otr::OTR_MESSAGESTATE_FINISHED)
			{
				otrstate = tr("Finished");
			}
			else
				if (state == otr::OTR_MESSAGESTATE_UNKNOWN)
				{
					otrstate = tr("Unknown");
				}
				return otrstate;
}

void OtrE2E::onMenuActionTriggered(QAction* a)
{
	OtrSessPair sp = a->data().value<OtrSessPair>();
	//qDebug(qPrintable(QString("%1/%2").arg(sp.localId(), sp.partnerId())));
	if (sp.actType() == OtrSessPair::Stop)
	{
		m_otr->endSession(sp.localId(),  sp.partnerId());
	}
	else
		if (sp.actType() == OtrSessPair::Refresh)
		{
			m_otr->endSession(sp.localId(),  sp.partnerId());
			m_otr->startSession(sp.localId(),  sp.partnerId());
		}
}

void OtrE2E::goneSecure(const QString& account, const QString& contact)
{
	Q_UNUSED(account);
	Q_UNUSED(contact);
	/*QString accId =m_accountManager->accountByStream(ew->streamJid())->accountId().toString();
	QString jid = ew->contactJid().full();*/
	/*qDebug(__PRETTY_FUNCTION__);
	qDebug(qPrintable(QString("%1/%2").arg(account).arg(contact)));*/
}

void OtrE2E::goneInsecure(const QString& account, const QString& contact)
{
	Q_UNUSED(account);
	Q_UNUSED(contact);
	/*QString accId =m_accountManager->accountByStream(ew->streamJid())->accountId().toString();
	QString jid = ew->contactJid().full();*/
	/*qDebug(__PRETTY_FUNCTION__);*/
	/*qDebug(qPrintable(QString("%1/%2").arg(account).arg(contact)));*/
}

void OtrE2E::updateContexts()
{
	//qDebug(__PRETTY_FUNCTION__);
	for (QHash<Action*, QToolButton*>::iterator i = m_buttons.begin(); i != m_buttons.end(); ++i)
	{
		Action *action = i.key();
		if (action)
		{
			IToolBarWidget *widget = m_actions.key(action);
			if (!widget)
				return;

			IEditWidget* ew = widget->editWidget();
			if (ew)
			{
				QToolButton* btn = i.value();
				QString accId =m_accountManager->accountByStream(ew->streamJid())->accountId().toString();
				QString jid = ew->contactJid().full();
				otr::OtrMessageState state = m_otr->getMessageState(accId, jid);

				QString otrstate = stateToString(state);

				btn->setText(QString("OTR: %1").arg(otrstate));
			}
		}
	}
}
