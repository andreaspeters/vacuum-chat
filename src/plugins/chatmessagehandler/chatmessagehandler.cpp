#include "chatmessagehandler.h"
#include "unicodeavatar.h"
#include "protocolmessagerouting.h"
#include "protocolmessagehistory.h"
#include "roomsidebarstate.h"
#include <interfaces/matrixreply.h>
#include <utils/systemtimezonecache.h>
#include <utils/matrixhtml.h>
#include <utils/imageloadscheduler.h>
#include <utils/animatedtextbrowser.h>
#include <QKeyEvent>
#include <utils/roundedavatar.h>
#include <utils/messagenotificationmute.h>
#include <interfaces/iemoticons.h>

#include <QMouseEvent>
#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QMimeDatabase>
#include <QInputDialog>
#include <QMenu>
#include <QLineEdit>
#include <algorithm>
#include <utility>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QLabel>
#include <QPixmap>
#include <QColor>
#include <QFrame>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QMessageBox>
#include <QTextEdit>
#include <QTextDocument>
#include <QTextBoundaryFinder>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QImage>
#include <QUrl>
#include <QUrlQuery>
#include <QPointer>
#include <QHash>
#include <QThreadPool>
#include <QRunnable>
#include <QScrollBar>

static void notifyImageResourceUpdated(QTextEdit *view, const QUrl &resourceUrl)
{
	if (AnimatedTextBrowser *browser = qobject_cast<AnimatedTextBrowser *>(view))
		emit browser->resourceUpdated(resourceUrl);
}

#define HISTORY_MESSAGES          10
#define HISTORY_TIME_DELTA        5
#define HISTORY_DUBLICATE_DELTA   2*60

#define ADR_STREAM_JID            Action::DR_StreamJid
#define ADR_CONTACT_JID           Action::DR_Parametr1

static void loadImagePixmapAsync(QLabel *label, const QString &path, const QSize &targetSize)
{
	if (!label || path.isEmpty())
		return;
	QPointer<QLabel> guardedLabel(label);
	const QString identity = path + QLatin1Char('|') + QString::number(targetSize.width()) +
		QLatin1Char('x') + QString::number(targetSize.height());
	label->setProperty("vacuum.imageLoad.identity", identity);
	ImageLoadScheduler *scheduler = ImageLoadScheduler::instance();
	if (!scheduler)
		return;
	scheduler->loadFile(path, label, [guardedLabel, identity, targetSize](const QImage &sourceImage) {
		if (!guardedLabel || guardedLabel->property("vacuum.imageLoad.identity").toString() != identity ||
			sourceImage.isNull())
			return;
		const QImage image = RoundedAvatar::roundImageScaled(sourceImage, targetSize);
		guardedLabel->setPixmap(QPixmap::fromImage(image));
		if (guardedLabel->property("matrixAvatarKey").isValid()) {
			guardedLabel->setProperty("matrixAvatarHasLoaded", true);
			guardedLabel->setProperty("matrixAvatarIsPlaceholder", false);
		}
	});
}

static QString cacheReplyAvatarResource(QTextEdit *view, const QString &path, QObject *receiver)
{
	if (!view || path.isEmpty())
		return QString();
	const QString key = ImageLoadScheduler::fileKey(path);
	QUrl resourceUrl;
	resourceUrl.setScheme(QStringLiteral("vacuum-avatar"));
	resourceUrl.setPath(QString::fromLatin1(QCryptographicHash::hash(
		key.toUtf8(), QCryptographicHash::Sha256).toHex()));
	QTextDocument *document = view->document();
	if (!document->resource(QTextDocument::ImageResource, resourceUrl).isValid()) {
		QImage placeholder(1, 1, QImage::Format_ARGB32_Premultiplied);
		placeholder.fill(Qt::transparent);
		document->addResource(QTextDocument::ImageResource, resourceUrl, placeholder);
		if (ImageLoadScheduler *scheduler = ImageLoadScheduler::instance()) {
			QPointer<QTextEdit> guardedView(view);
			scheduler->loadFile(path, receiver, [guardedView, resourceUrl](const QImage &image) {
				if (!guardedView || image.isNull())
					return;
				QTextDocument *doc = guardedView->document();
				doc->addResource(QTextDocument::ImageResource, resourceUrl, image);
				doc->markContentsDirty(0, doc->characterCount());
				guardedView->viewport()->update();
				notifyImageResourceUpdated(guardedView, resourceUrl);
			});
		}
	}
	return resourceUrl.toString(QUrl::FullyEncoded);
}

static QString buildProtocolReplyPreviewHtml(const QList<BasicMessage> &history,
	IChatWindow *AWindow, IProtocolMessaging *AMessaging, const QString &AReplyEventId,
	QObject *receiver);

ChatMessageHandler::ChatMessageHandler()
{
	FMessageWidgets = NULL;
	FMessageProcessor = NULL;
	FPluginManager = NULL;
	FMessageStyles = NULL;
	FPresencePlugin = NULL;
	FMessageArchiver = NULL;
	FRostersView = NULL;
	FRostersModel = NULL;
	FStatusIcons = NULL;
	FStatusChanger = NULL;
	FXmppUriQueries = NULL;
	FOptionsManager = NULL;
	FRecentContacts = NULL;
	FFileTransfer = NULL;
	FProtocolRoster = NULL;
	FReplyEscFilter = NULL;
	FRoomInviteTarget.clear();
}

ChatMessageHandler::~ChatMessageHandler()
{

}

void ChatMessageHandler::pluginInfo(IPluginInfo *APluginInfo)
{
	APluginInfo->name = tr("Chat Messages");
	APluginInfo->description = tr("Allows to exchange chat messages");
	APluginInfo->version = "1.0";
	APluginInfo->author = "Potapov S.A. aka Lion";
	APluginInfo->homePage = "https://github.com/andreaspeters/vacuum-chat";
	APluginInfo->dependences.append(MESSAGEWIDGETS_UUID);
	APluginInfo->dependences.append(MESSAGEPROCESSOR_UUID);
	APluginInfo->dependences.append(MESSAGESTYLES_UUID);
}


bool ChatMessageHandler::initConnections(IPluginManager *APluginManager, int &AInitOrder)
{
	Q_UNUSED(AInitOrder);
	FPluginManager = APluginManager;

	IPlugin *plugin = APluginManager->pluginInterface("IMessageWidgets").value(0,NULL);
	if (plugin)
		FMessageWidgets = qobject_cast<IMessageWidgets *>(plugin->instance());
	plugin = APluginManager->pluginInterface("IFileTransfer").value(0,NULL);
	if (plugin)
		FFileTransfer = qobject_cast<IFileTransfer *>(plugin->instance());
	plugin = APluginManager->pluginInterface("IProtocolRoster").value(0,NULL);
	if (plugin) {
		FProtocolRoster = qobject_cast<IProtocolRoster *>(plugin->instance());
		connect(plugin->instance(), SIGNAL(protocolRosterChanged()),
			this, SLOT(onProtocolRosterChanged()), Qt::UniqueConnection);
	}

	plugin = APluginManager->pluginInterface("IMessageProcessor").value(0,NULL);
	if (plugin)
		FMessageProcessor = qobject_cast<IMessageProcessor *>(plugin->instance());

	plugin = APluginManager->pluginInterface("IMessageStyles").value(0,NULL);
	if (plugin)
	{
		FMessageStyles = qobject_cast<IMessageStyles *>(plugin->instance());
		if (FMessageStyles)
		{
			connect(FMessageStyles->instance(),SIGNAL(styleOptionsChanged(const IMessageStyleOptions &, int, const QString &)),
				SLOT(onStyleOptionsChanged(const IMessageStyleOptions &, int, const QString &)));
		}
	}

	plugin = APluginManager->pluginInterface("IStatusIcons").value(0,NULL);
	if (plugin)
	{
		FStatusIcons = qobject_cast<IStatusIcons *>(plugin->instance());
		if (FStatusIcons)
		{
			connect(FStatusIcons->instance(),SIGNAL(statusIconsChanged()),SLOT(onStatusIconsChanged()));
		}
	}

	plugin = APluginManager->pluginInterface("IPresencePlugin").value(0,NULL);
	if (plugin)
	{
		FPresencePlugin = qobject_cast<IPresencePlugin *>(plugin->instance());
		if (FPresencePlugin)
		{
			connect(FPresencePlugin->instance(),SIGNAL(presenceItemReceived(IPresence *, const IPresenceItem &, const IPresenceItem &)),
				SLOT(onPresenceItemReceived(IPresence *, const IPresenceItem &, const IPresenceItem &)));
		}
	}

	plugin = APluginManager->pluginInterface("IMessageArchiver").value(0,NULL);
	if (plugin)
	{
		FMessageArchiver = qobject_cast<IMessageArchiver *>(plugin->instance());
		if (FMessageArchiver)
		{
			connect(FMessageArchiver->instance(),SIGNAL(messagesLoaded(const QString &, const IArchiveCollectionBody &)),
				SLOT(onArchiveMessagesLoaded(const QString &, const IArchiveCollectionBody &)));
			connect(FMessageArchiver->instance(),SIGNAL(requestFailed(const QString &, const XmppError &)),
				SLOT(onArchiveRequestFailed(const QString &, const XmppError &)));
		}
	}

	plugin = APluginManager->pluginInterface("INotifications").value(0,NULL);
	if (plugin)
	{
		INotifications *notifications = qobject_cast<INotifications *>(plugin->instance());
		if (notifications)
		{
			INotificationType notifyType;
			notifyType.order = NTO_CHATHANDLER_MESSAGE;
			notifyType.icon = IconStorage::staticStorage(RSR_STORAGE_MENUICONS)->getIcon(MNI_CHAT_MHANDLER_MESSAGE);
			notifyType.title = tr("When receiving new chat message");
			notifyType.kindMask = INotification::RosterNotify|INotification::PopupWindow|INotification::TrayNotify|INotification::TrayAction|INotification::SoundPlay|INotification::AlertWidget|INotification::TabPageNotify|INotification::ShowMinimized|INotification::AutoActivate;
			notifyType.kindDefs = notifyType.kindMask & ~(INotification::AutoActivate);
			notifications->registerNotificationType(NNT_CHAT_MESSAGE,notifyType);
		}
	}

	plugin = APluginManager->pluginInterface("IRostersViewPlugin").value(0,NULL);
	if (plugin)
	{
		IRostersViewPlugin *rostersViewPlugin = qobject_cast<IRostersViewPlugin *>(plugin->instance());
		if (rostersViewPlugin)
		{
			FRostersView = rostersViewPlugin->rostersView();
			connect(FRostersView->instance(),SIGNAL(indexContextMenu(const QList<IRosterIndex *> &, quint32, Menu *)),
				SLOT(onRosterIndexContextMenu(const QList<IRosterIndex *> &, quint32, Menu *)));
		}
	}

	plugin = APluginManager->pluginInterface("IRostersModel").value(0,NULL);
	if (plugin)
		FRostersModel = qobject_cast<IRostersModel *>(plugin->instance());

	plugin = APluginManager->pluginInterface("IStatusChanger").value(0,NULL);
	if (plugin)
		FStatusChanger = qobject_cast<IStatusChanger *>(plugin->instance());

	plugin = APluginManager->pluginInterface("IXmppUriQueries").value(0,NULL);
	if (plugin)
		FXmppUriQueries = qobject_cast<IXmppUriQueries *>(plugin->instance());

	plugin = APluginManager->pluginInterface("IOptionsManager").value(0,NULL);
	if (plugin)
		FOptionsManager = qobject_cast<IOptionsManager *>(plugin->instance());

	plugin = APluginManager->pluginInterface("IRecentContacts").value(0,NULL);
	if (plugin)
		FRecentContacts = qobject_cast<IRecentContacts *>(plugin->instance());

	connect(Shortcuts::instance(),SIGNAL(shortcutActivated(const QString &, QWidget *)),SLOT(onShortcutActivated(const QString &, QWidget *)));

	return FMessageProcessor!=NULL && FMessageWidgets!=NULL && FMessageStyles!=NULL;
}

bool ChatMessageHandler::initObjects()
{
	Shortcuts::declareShortcut(SCT_MESSAGEWINDOWS_CHAT_CLEARWINDOW, tr("Clear window"), QKeySequence::UnknownKey);
	Shortcuts::declareShortcut(SCT_ROSTERVIEW_SHOWCHATDIALOG, tr("Open chat dialog"), tr("Return","Open chat dialog"), Shortcuts::WidgetShortcut);

	if (FRostersView)
	{
		FRostersView->insertClickHooker(RCHO_CHATMESSAGEHANDLER,this);
		Shortcuts::insertWidgetShortcut(SCT_ROSTERVIEW_SHOWCHATDIALOG,FRostersView->instance());
	}
	if (FMessageProcessor)
	{
		FMessageProcessor->insertMessageHandler(MHO_CHATMESSAGEHANDLER,this);
	}
	if (FXmppUriQueries)
	{
		FXmppUriQueries->insertUriHandler(this, XUHO_DEFAULT);
	}
	return true;
}

bool ChatMessageHandler::initSettings()
{
	Options::setDefaultValue(OPV_MESSAGES_LOAD_HISTORY, true);
	if (FOptionsManager)
		FOptionsManager->insertOptionsHolder(this);
	return true;
}

QMultiMap<int, IOptionsWidget *> ChatMessageHandler::optionsWidgets(const QString &ANodeId, QWidget *AParent)
{
	QMultiMap<int, IOptionsWidget *> widgets;
	if (FOptionsManager && ANodeId == OPN_MESSAGES)
	{
		widgets.insertMulti(OWO_MESSAGES_LOADHISTORY,FOptionsManager->optionsNodeWidget(Options::node(OPV_MESSAGES_LOAD_HISTORY),tr("Load messages from history in new chat windows"),AParent));
	}
	return widgets;
}

bool ChatMessageHandler::xmppUriOpen(const Jid &AStreamJid, const Jid &AContactJid, const QString &AAction, const QMultiMap<QString, QString> &AParams)
{
	if (AAction == "message")
	{
		QString type = AParams.value("type");
		if (type == "chat")
		{
			IChatWindow *window = getWindow(AStreamJid, AContactJid);
			window->editWidget()->textEdit()->setPlainText(AParams.value("body"));
			window->showTabPage();
			return true;
		}
	}
	return false;
}

bool ChatMessageHandler::rosterIndexSingleClicked(int AOrder, IRosterIndex *AIndex, const QMouseEvent *AEvent)
{
	if (AIndex && !AIndex->data(RDR_CONVERSATION_ID).toString().isEmpty())
		return rosterIndexDoubleClicked(AOrder, AIndex, AEvent);
	if (Options::node(OPV_MESSAGES_COMBINEWITHROSTER).value().toBool())
		return rosterIndexDoubleClicked(AOrder, AIndex, AEvent);
	return false;
}

bool ChatMessageHandler::rosterIndexDoubleClicked(int AOrder, IRosterIndex *AIndex, const QMouseEvent *AEvent)
{
	Q_UNUSED(AOrder);
	if (!FMessageWidgets && FPluginManager) {
		IPlugin *plugin = FPluginManager->pluginInterface("IMessageWidgets").value(0, NULL);
		if (plugin)
			FMessageWidgets = qobject_cast<IMessageWidgets *>(plugin->instance());
	}
	if (FPluginManager) {
		for (IPlugin *plugin : FPluginManager->pluginInterface("IProtocolMessaging")) {
			IProtocolMessaging *messaging = qobject_cast<IProtocolMessaging *>(plugin->instance());
			if (messaging && !FProtocolMessaging.contains(messaging))
				FProtocolMessaging.append(messaging);
		}
	}
	const QString accountId = AIndex->data(RDR_ACCOUNT_ID).toString();
	const QString conversationId = AIndex->data(RDR_CONVERSATION_ID).toString();
	const bool isConversationIndex = !conversationId.isEmpty();
	if (AEvent->modifiers()==Qt::NoModifier &&
		(AIndex->type()==RIT_CONTACT || AIndex->type()==RIT_MY_RESOURCE || isConversationIndex))
	{
		if (!conversationId.isEmpty() && FMessageWidgets)
		{
			IProtocolMessaging *selectedMessaging = nullptr;
			for (IProtocolMessaging *messaging : FProtocolMessaging) {
				if (messaging && ProtocolMessageRouting::hasExactStream(
					messaging->streamId(), accountId)) {
					selectedMessaging = messaging;
					break;
				}
			}
			if (!selectedMessaging)
				return false;
			IChatWindow *window = FMessageWidgets->getConversationWindow(
				selectedMessaging->streamId(), conversationId);
			if (window)
			{
				setupProtocolWindow(window, selectedMessaging);
				renderProtocolHistory(window, selectedMessaging);
				window->showTabPage();
			}
			return window != NULL;
		}
		Jid streamJid = AIndex->data(RDR_STREAM_JID).toString();
		Jid contactJid = AIndex->data(RDR_FULL_JID).toString();
		return messageShowWindow(MHO_CHATMESSAGEHANDLER,streamJid,contactJid,Message::Chat,IMessageHandler::SM_SHOW);
	}
	return false;
}

bool ChatMessageHandler::startPlugin()
{
	if (FPluginManager) {
		if (!FProtocolRoster) {
			IPlugin *rosterPlugin = FPluginManager->pluginInterface("IProtocolRoster").value(0, NULL);
			if (rosterPlugin) {
				FProtocolRoster = qobject_cast<IProtocolRoster *>(rosterPlugin->instance());
				connect(rosterPlugin->instance(), SIGNAL(protocolRosterChanged()),
					this, SLOT(onProtocolRosterChanged()), Qt::UniqueConnection);
			}
		}

		foreach (IPlugin *plugin, FPluginManager->pluginInterface("IProtocolMessaging"))
		{
			IProtocolMessaging *messaging = qobject_cast<IProtocolMessaging *>(plugin->instance());
			if (messaging && !FProtocolMessaging.contains(messaging))
			{
				FProtocolMessaging.append(messaging);
				connect(plugin->instance(), SIGNAL(protocolMessageReceived(BasicMessage)), this, SLOT(onProtocolMessageReceived(BasicMessage)), Qt::UniqueConnection);
				if (messaging->protocol() == QStringLiteral("matrix"))
					connect(plugin->instance(), SIGNAL(protocolAvatarUpdated(QString)),
						this, SLOT(onProtocolAvatarUpdated(QString)), Qt::UniqueConnection);
				if (messaging->protocol() == QStringLiteral("matrix"))
					connect(plugin->instance(), SIGNAL(protocolHistoryLoaded(QString)),
						this, SLOT(onProtocolHistoryLoaded(QString)), Qt::UniqueConnection);
			}
		}
	}
	return true;
}

void ChatMessageHandler::onProtocolViewContextMenu(const QPoint &APosition,
	const QTextDocumentFragment &ASelection, Menu *AMenu)
{
	Q_UNUSED(APosition);
	Q_UNUSED(ASelection);
	if (!AMenu)
		return;

	IChatWindow *window = nullptr;
	for (IChatWindow *candidate : std::as_const(FWindows))
		if (candidate && candidate->viewWidget()->instance() == sender()) {
			window = candidate;
			break;
		}
	if (!window)
		return;

	const QString displayedMessageId = AMenu->property("vacuum.messageId").toString();
	if (displayedMessageId.isEmpty())
		return;
	IProtocolMessaging *messaging = nullptr;
	for (IProtocolMessaging *candidate : std::as_const(FProtocolMessaging))
		if (candidate && candidate->streamId() == window->accountId() &&
			candidate->protocol() == QStringLiteral("matrix") &&
			candidate->supportsReactions(window->conversationId())) {
			messaging = candidate;
			break;
		}
	if (!messaging)
		return;

	const QString eventPrefix = messaging->streamId() + QChar('\n') +
		window->conversationId() + QChar('\n');
	QString eventId;
	for (auto it = FProtocolEventMessageIds.constBegin(); it != FProtocolEventMessageIds.constEnd(); ++it)
		if (it.key().startsWith(eventPrefix) && it.value() == displayedMessageId) {
			const QString candidateId = it.key().mid(eventPrefix.size());
			if (MatrixReply::isEventId(candidateId)) {
				eventId = candidateId;
				break;
			}
		}
	const QString targetKey = eventPrefix + eventId;
	const QString targetRelationType = FProtocolMessageRelationTypes.value(targetKey);
	if (eventId.isEmpty() || FProtocolRedactedMessages.contains(targetKey) ||
		FProtocolReactionEvents.contains(targetKey) ||
		targetRelationType == QStringLiteral("m.annotation") ||
		targetRelationType == QStringLiteral("m.replace"))
		return;

	QString targetSenderName = tr("Unknown sender");
	const QString historyKey = messaging->streamId() + QChar('\n') + window->conversationId();
	for (const BasicMessage &target : FProtocolConversationMessages.value(historyKey))
		if (target.messageId() == eventId) {
			targetSenderName = target.metadata().value(QStringLiteral("sender_name")).toString();
			if (targetSenderName.isEmpty())
				targetSenderName = target.sender();
			break;
		}

	// Add Reply action above React.
	Action *replyAction = new Action(AMenu);
	replyAction->setText(tr("Reply"));
	connect(replyAction, &QAction::triggered, this,
		[this, window, eventId, targetSenderName]() {
			if (!window || !window->editWidget())
				return;
			window->showTabPage();
			QTextEdit *editor = window->editWidget()->textEdit();
			if (!editor)
				return;
			if (editor->property("vacuum.matrix.reply_event_id").toString().isEmpty())
				editor->setProperty("vacuum.matrix.reply_original_placeholder", editor->placeholderText());
			editor->setProperty("vacuum.matrix.reply_event_id", eventId);
			editor->setPlaceholderText(tr("Replying to %1").arg(targetSenderName));
			editor->setFocus(Qt::OtherFocusReason);
		});
	AMenu->addAction(replyAction, AG_DEFAULT, true);

	AMenu->addSeparator(); // Add separator between Reply and React

	QMenu *reactionMenu = AMenu->addMenu(tr("React"));
	const QStringList quickReactions = {
		QStringLiteral("👍"), QStringLiteral("👎"), QStringLiteral("😀"),
		QStringLiteral("🎉"), QStringLiteral("😐"), QStringLiteral("❤️"),
		QStringLiteral("🚀"), QStringLiteral("👀")};
	for (const QString &key : quickReactions) {
		QAction *action = reactionMenu->addAction(key);
		connect(action, &QAction::triggered, this,
			[messaging, conversationId = window->conversationId(), eventId, key]() {
				messaging->sendReaction(conversationId, eventId, key);
			});
	}
	reactionMenu->addSeparator();
	QAction *moreAction = reactionMenu->addAction(tr("More…"));
	connect(moreAction, &QAction::triggered, this,
		[this, messaging, parent = window->instance(),
		 conversationId = window->conversationId(), eventId]() {
			bool accepted = false;
			const QString key = QInputDialog::getText(parent, tr("Add a reaction"),
				tr("Emoji or reaction text:"), QLineEdit::Normal, QString(), &accepted).trimmed();
			if (accepted && !key.isEmpty())
				messaging->sendReaction(conversationId, eventId, key);
		});
}

void ChatMessageHandler::onProtocolRosterChanged()
{
	for (IChatWindow *window : FWindows) {
		if (!window)
			continue;
		for (IProtocolMessaging *messaging : FProtocolMessaging)
			if (messaging && messaging->streamId() == window->accountId()) {
				setupRoomSidebar(window, messaging);
				break;
			}
	}
}

void ChatMessageHandler::onProtocolAvatarUpdated(const QString &key)
{
	for (QWidget *widget : QApplication::allWidgets()) {
		QLabel *avatar = qobject_cast<QLabel *>(widget);
		if (!avatar || avatar->property("matrixAvatarKey").toString() != key)
			continue;
		const QString accountId = avatar->property("matrixAccountId").toString();
		const QString roomId = avatar->property("matrixRoomId").toString();
		const QString userId = avatar->property("matrixUserId").toString();
		for (IProtocolMessaging *messaging : FProtocolMessaging)
			if (messaging && messaging->streamId() == accountId) {
				loadImagePixmapAsync(avatar, messaging->userAvatarPath(roomId, userId), avatar->size());
				break;
			}
	}
	for (IChatWindow *window : FWindows) {
		if (!window || window->conversationId().isEmpty())
			continue;
		for (IProtocolMessaging *messaging : FProtocolMessaging)
			if (messaging && messaging->streamId() == window->accountId()) {
				const QString avatarPath = messaging->conversationAvatarPath(window->conversationId());
				if (!avatarPath.isEmpty())
					window->infoWidget()->setField(IInfoWidget::ContactAvatar, avatarPath);
				break;
			}
	}
}

void ChatMessageHandler::onProtocolMessageReceived(const BasicMessage &AMessage)
{
	IProtocolMessaging *messaging = qobject_cast<IProtocolMessaging *>(sender());
	if (!messaging || !ProtocolMessageRouting::matchesProtocol(
		AMessage.protocol(), messaging->protocol()) || messaging->streamId().isEmpty())
		return;
	{
		IChatWindow *window = FMessageWidgets->findConversationWindow(messaging->streamId(), AMessage.conversationId());
		const QString messageType = AMessage.metadata().value(QStringLiteral("msgtype")).toString();
		const bool pendingMedia = (messageType == QStringLiteral("m.image") ||
			messageType == QStringLiteral("m.file")) &&
			AMessage.metadata().value(QStringLiteral("decoded_image")).value<QImage>().isNull() &&
			AMessage.metadata().value(QStringLiteral("file_path")).toString().isEmpty();
		if (!window && pendingMedia)
			return;
		const bool historical = AMessage.metadata().value(QStringLiteral("historical")).toBool();
		// Initial-sync history is already persisted in SQLite and must not
		// create one chat window per room on the UI thread. Render it only when
		// the user has already opened that conversation; a later open loads the
		// history through conversationHistory().
		if (!window && historical)
			return;
		const bool firstMessage = !window;
		if (!window) window = FMessageWidgets->getConversationWindow(messaging->streamId(), AMessage.conversationId());
		if (!window) return;
		setupProtocolWindow(window, messaging);
		if (firstMessage)
			renderProtocolHistory(window, messaging);
		const QString historyKey = messaging->streamId() + QChar('\n') + AMessage.conversationId();
		if (FProtocolHistoryLoading.contains(historyKey)) {
			QList<BasicMessage> &pending = FPendingProtocolHistoryMessages[historyKey];
			if (ProtocolMessageHistory::mergeTransactionEcho(pending, AMessage) ==
				ProtocolMessageHistory::TransactionEchoMergeResult::NoMatch)
				pending.append(AMessage);
			return;
		}
		QList<BasicMessage> &history = FProtocolConversationMessages[historyKey];
		const int existingIndex = ProtocolMessageHistory::findMessageIndex(history,
			AMessage.messageId());
		const bool mediaHydration = existingIndex >= 0 &&
			ProtocolMessageHistory::isMediaHydrationUpdate(history.at(existingIndex), AMessage);
		const int duplicateIndex = ProtocolMessageHistory::replaceExistingMessage(history, AMessage);
		if (duplicateIndex >= 0) {
			const QString prefix = messaging->streamId() + QChar('\n') +
				AMessage.conversationId() + QChar('\n');
			const QString messageKey = prefix + AMessage.messageId();
			const bool alreadyRendered = FProtocolRenderedMessages.contains(messageKey) ||
				FProtocolRenderedMessages.contains(messageKey + QStringLiteral("|image"));
			sortProtocolMessagesChronologically(history);
			if (mediaHydration) {
				// Replace the rendered placeholder by rebuilding the timeline from the updated event.
				scheduleProtocolConversationRebuild(window, messaging, historyKey, sender());
			} else if (!alreadyRendered) {
				const bool outOfOrder = duplicateIndex + 1 < history.size();
				if (outOfOrder)
					scheduleProtocolConversationRebuild(window, messaging, historyKey, sender());
				else
					renderProtocolMessage(window, messaging, AMessage);
			}
			return;
		}
		if (FProtocolHistoryLoaded.contains(historyKey)) {
			const int previousIndex = ProtocolMessageHistory::findTransactionEchoIndex(history, AMessage);
			const QString previousMessageId = previousIndex >= 0 ?
				history.at(previousIndex).messageId() : QString();
			const QString prefix = messaging->streamId() + QChar('\n') +
				AMessage.conversationId() + QChar('\n');
			const QString previousMessageKey = prefix + previousMessageId;
			const bool previouslyRendered = previousIndex >= 0 &&
				(FProtocolRenderedMessages.contains(previousMessageKey) ||
				 FProtocolRenderedMessages.contains(previousMessageKey + QStringLiteral("|image")));
			const ProtocolMessageHistory::TransactionEchoMergeResult transactionMerge =
				ProtocolMessageHistory::mergeTransactionEcho(history, AMessage);
			if (transactionMerge != ProtocolMessageHistory::TransactionEchoMergeResult::NoMatch) {
				if (transactionMerge == ProtocolMessageHistory::TransactionEchoMergeResult::IgnoredLocalEcho)
					return;
				sortProtocolMessagesChronologically(history);
				const QString currentMessageId = transactionMerge ==
					ProtocolMessageHistory::TransactionEchoMergeResult::RetainedLocalEcho ?
					previousMessageId : AMessage.messageId();
				int currentIndex = ProtocolMessageHistory::findMessageIndex(history,
					currentMessageId);
				if (ProtocolMessageHistory::requiresChronologicalSort(history, currentIndex)) {
					sortProtocolMessagesChronologically(history);
					currentIndex = ProtocolMessageHistory::findMessageIndex(history, currentMessageId);
				}
				if (ProtocolMessageHistory::requiresTimelineRebuild(previousIndex, currentIndex,
					history.size(), previouslyRendered)) {
					scheduleProtocolConversationRebuild(window, messaging, historyKey, sender());
				} else if (transactionMerge ==
					ProtocolMessageHistory::TransactionEchoMergeResult::ReplacedLocalEcho ||
					transactionMerge == ProtocolMessageHistory::TransactionEchoMergeResult::RestoredLocalEcho) {
					renderProtocolMessage(window, messaging, AMessage);
				} else if (!previouslyRendered && currentIndex >= 0) {
					renderProtocolMessage(window, messaging, history.at(currentIndex));
				}
				return;
			}
		}
		const bool outOfOrder = !history.isEmpty() && AMessage.timestamp().isValid() &&
			history.last().timestamp().isValid() &&
			AMessage.timestamp() < history.last().timestamp();
		history.append(AMessage);
		sortProtocolMessagesChronologically(history);
		if (outOfOrder) {
			scheduleProtocolConversationRebuild(window, messaging, historyKey, sender());
		} else
			renderProtocolMessage(window, messaging, AMessage);
		return;
	}
}

void ChatMessageHandler::onProtocolHistoryLoaded(const QString &ARoomId)
{
	IProtocolMessaging *messaging = qobject_cast<IProtocolMessaging *>(sender());
	if (!messaging || ARoomId.isEmpty())
		return;
	const QString historyKey = messaging->streamId() + QChar('\n') + ARoomId;
	if (!FProtocolHistoryLoading.remove(historyKey))
		return;
	const bool hadHistory = FProtocolHistoryLoaded.contains(historyKey);
	const QList<BasicMessage> previousHistory = FProtocolConversationMessages.value(historyKey);
	FProtocolHistoryLoaded.insert(historyKey);
	QList<BasicMessage> history = FPendingProtocolHistoryMessages.take(historyKey);
	sortProtocolMessagesChronologically(history);
	QHash<QString, int> messageIndexes;
	QList<BasicMessage> uniqueMessages;
	for (const BasicMessage &message : history) {
		const ProtocolMessageHistory::TransactionEchoMergeResult transactionMerge =
			ProtocolMessageHistory::mergeTransactionEcho(uniqueMessages, message);
		if (transactionMerge != ProtocolMessageHistory::TransactionEchoMergeResult::NoMatch) {
			if (transactionMerge == ProtocolMessageHistory::TransactionEchoMergeResult::ReplacedLocalEcho ||
				transactionMerge == ProtocolMessageHistory::TransactionEchoMergeResult::RestoredLocalEcho) {
				messageIndexes.clear();
				for (int index = 0; index < uniqueMessages.size(); ++index)
					if (!uniqueMessages.at(index).messageId().isEmpty())
						messageIndexes.insert(uniqueMessages.at(index).messageId(), index);
			}
			continue;
		}
		if (message.messageId().isEmpty() || !messageIndexes.contains(message.messageId())) {
			if (!message.messageId().isEmpty())
				messageIndexes.insert(message.messageId(), uniqueMessages.size());
			uniqueMessages.append(message);
		} else {
			uniqueMessages[messageIndexes.value(message.messageId())] = message;
		}
	}
	sortProtocolMessagesChronologically(uniqueMessages);
	FProtocolConversationMessages.insert(historyKey, uniqueMessages);
	IChatWindow *window = FMessageWidgets->findConversationWindow(messaging->streamId(), ARoomId);
	if (!window)
		return;
	bool hasOutOfOrderNewMessage = false;
	if (hadHistory && !previousHistory.isEmpty()) {
		QSet<QString> previousIds;
		for (const BasicMessage &message : previousHistory)
			previousIds.insert(message.messageId());
		for (const BasicMessage &message : uniqueMessages)
			if (!previousIds.contains(message.messageId()) && message.timestamp().isValid() &&
				previousHistory.last().timestamp().isValid() &&
				message.timestamp() < previousHistory.last().timestamp()) {
				hasOutOfOrderNewMessage = true;
				break;
			}
	}
	if (hasOutOfOrderNewMessage) {
		scheduleProtocolConversationRebuild(window, messaging, historyKey, sender());
		return;
	}
	for (const BasicMessage &message : uniqueMessages)
		renderProtocolMessage(window, messaging, message);
}

void ChatMessageHandler::renderProtocolMessage(IChatWindow *AWindow, IProtocolMessaging *AMessaging,
	const BasicMessage &AMessage)
{
	if (!AWindow || !AMessaging)
		return;
	const QString prefix = AMessaging->streamId() + QChar('\n') + AMessage.conversationId() + QChar('\n');
	const QString redactedEventId = AMessage.metadata().value(QStringLiteral("redacts")).toString();
	if (!redactedEventId.isEmpty()) {
		if (FProtocolReactionEvents.contains(prefix + redactedEventId)) {
			removeProtocolReaction(AWindow, prefix, redactedEventId);
			return;
		}
		const QString targetKey = prefix + redactedEventId;
		FProtocolRedactedMessages.insert(targetKey);
		const auto reactions = FProtocolReactionSenders.take(targetKey);
		for (auto reactionIt = reactions.constBegin(); reactionIt != reactions.constEnd(); ++reactionIt)
			for (auto senderIt = reactionIt.value().constBegin(); senderIt != reactionIt.value().constEnd(); ++senderIt)
				for (const QString &sourceKey : senderIt.value())
					FProtocolReactionEvents.remove(sourceKey);
		const QString displayMessageId = FProtocolEventMessageIds.value(targetKey, redactedEventId);
		AWindow->viewWidget()->setMessageDecoration(displayMessageId,
			QStringLiteral("matrix-reactions"), QString());
		const QString historyKey = AMessaging->streamId() + QChar('\n') +
			AMessage.conversationId();
		auto historyIt = FProtocolConversationMessages.find(historyKey);
		if (historyIt != FProtocolConversationMessages.end()) {
			auto target = std::find_if(historyIt->begin(), historyIt->end(),
				[&redactedEventId](const BasicMessage &message) {
					return message.messageId() == redactedEventId;
				});
			if (target != historyIt->end()) {
				QVariantMap metadata = target->metadata();
				const bool changed = target->body() != QStringLiteral("message deleted") ||
					!metadata.value(QStringLiteral("redacted")).toBool() ||
					metadata.contains(QStringLiteral("format")) ||
					metadata.contains(QStringLiteral("formatted_body")) ||
					metadata.contains(QStringLiteral("body")) ||
					metadata.contains(QStringLiteral("image_data")) ||
					metadata.contains(QStringLiteral("url")) ||
					metadata.contains(QStringLiteral("file"));
				BasicMessage redactedMessage = *target;
				redactedMessage.setBody(QStringLiteral("message deleted"));
				for (const QString &contentKey : {QStringLiteral("body"), QStringLiteral("format"),
					QStringLiteral("formatted_body"), QStringLiteral("m.new_content"),
					QStringLiteral("m.relates_to"), QStringLiteral("url"), QStringLiteral("file"),
					QStringLiteral("thumbnail_url"), QStringLiteral("filename"),
					QStringLiteral("attachments"), QStringLiteral("attachment_url"),
					QStringLiteral("attachment_file"), QStringLiteral("attachment_thumbnail_url"),
					QStringLiteral("image_data"), QStringLiteral("image_url"),
					QStringLiteral("file_path")})
					metadata.remove(contentKey);
				metadata.insert(QStringLiteral("msgtype"), QStringLiteral("m.text"));
				metadata.insert(QStringLiteral("redacted"), true);
				redactedMessage.setMetadata(metadata);
				*target = redactedMessage;
				historyIt->erase(std::remove_if(historyIt->begin(), historyIt->end(),
					[&AMessage](const BasicMessage &message) {
						return message.messageId() == AMessage.messageId();
					}), historyIt->end());
				if (changed) {
					sortProtocolMessagesChronologically(*historyIt);
					scheduleProtocolConversationRebuild(AWindow, AMessaging, historyKey,
						dynamic_cast<QObject *>(AMessaging));
				}
			}
		}
		return;
	}
	if (AMessage.metadata().value(QStringLiteral("outer_event_type")).toString() ==
		QStringLiteral("m.room.encrypted") &&
		AMessage.metadata().value(QStringLiteral("decryption_status")).toString() !=
			QStringLiteral("decrypted") &&
		!AMessage.metadata().value(QStringLiteral("redacted")).toBool())
		return;
	if (AMessage.metadata().value(QStringLiteral("event_type")).toString() ==
		QStringLiteral("m.reaction")) {
		addProtocolReaction(AWindow, AMessaging, AMessage);
		return;
	}
	const QString eventKey = prefix + AMessage.messageId();
	if (AMessage.metadata().value(QStringLiteral("redacted")).toBool()) {
		FProtocolRedactedMessages.insert(eventKey);
		const auto reactions = FProtocolReactionSenders.take(eventKey);
		for (auto reactionIt = reactions.constBegin(); reactionIt != reactions.constEnd(); ++reactionIt)
			for (auto senderIt = reactionIt.value().constBegin(); senderIt != reactionIt.value().constEnd(); ++senderIt)
				for (const QString &sourceKey : senderIt.value())
					FProtocolReactionEvents.remove(sourceKey);
	}
	if (!AMessage.messageId().isEmpty()) {
		const QString historyKey = AMessaging->streamId() + QChar('\n') + AMessage.conversationId();
		QList<BasicMessage> &history = FProtocolConversationMessages[historyKey];
		const ProtocolMessageHistory::TransactionEchoMergeResult transactionMerge =
			ProtocolMessageHistory::mergeTransactionEcho(history, AMessage);
		if (transactionMerge == ProtocolMessageHistory::TransactionEchoMergeResult::IgnoredLocalEcho)
			return;
		if (transactionMerge == ProtocolMessageHistory::TransactionEchoMergeResult::NoMatch) {
			auto cached = std::find_if(history.begin(), history.end(), [&AMessage](const BasicMessage &message) {
				return message.messageId() == AMessage.messageId();
			});
			if (cached == history.end())
				history.append(AMessage);
			else
				*cached = AMessage;
		}
	}
	QString messageKey = prefix + AMessage.messageId();
	if (!AMessage.metadata().value(QStringLiteral("decoded_image")).value<QImage>().isNull())
		messageKey += QStringLiteral("|image");
	const QString replacementKey = prefix + AMessage.metadata().value(QStringLiteral("replaces_txn_id")).toString();
	if (!replacementKey.endsWith(QChar('\n')) && FProtocolRenderedMessages.contains(replacementKey)) {
		FProtocolEventMessageIds.insert(eventKey,
			AMessage.metadata().value(QStringLiteral("replaces_txn_id")).toString());
		FProtocolMessageDirections.insert(eventKey, FProtocolMessageDirections.value(replacementKey, false));
		FProtocolMessageRelationTypes.insert(eventKey,
			AMessage.metadata().value(QStringLiteral("relation_type")).toString());
		updateProtocolReactionDecoration(AWindow, eventKey, AMessage.messageId());
		return;
	}
	if (FProtocolRenderedMessages.contains(messageKey))
		return;
	IMessageContentOptions options;
	options.kind = IMessageContentOptions::KindMessage;
	options.messageId = AMessage.messageId();
	const bool isVacuumUser = AMessage.direction() == BasicMessage::Outgoing ||
		AMessage.metadata().value(QStringLiteral("sender_is_self")).toBool();
	options.direction = isVacuumUser ? IMessageContentOptions::DirectionOut : IMessageContentOptions::DirectionIn;
	options.time = SystemTimeZoneCache::toSystemLocalTime(AMessage.timestamp());
	options.timeFormat = QStringLiteral("yyyy-MM-dd hh:mm:ss");
	options.senderId = AMessage.sender();
	const QString rawSenderName = AMessage.metadata().value(QStringLiteral("sender_name")).toString().isEmpty()
		? AMessage.sender()
		: AMessage.metadata().value(QStringLiteral("sender_name")).toString();
	options.senderName = rawSenderName.toHtmlEscaped();
	QString senderAvatar = AMessage.metadata().value(QStringLiteral("sender_avatar")).toString();
	if (senderAvatar.isEmpty())
	{
		senderAvatar = AMessaging->userAvatarPath(AMessage.conversationId(), AMessage.sender());
		if (senderAvatar.isEmpty())
			senderAvatar = UnicodeAvatar::getAvatarPath(rawSenderName);
	}
	options.senderAvatar = senderAvatar;
	options.senderColor = isVacuumUser ? QStringLiteral("red") : QStringLiteral("blue");
	QString body = AMessage.body();
	if (body.isEmpty())
		body = AMessage.metadata().value(QStringLiteral("body")).toString();
	if (body.isEmpty())
		body = AMessage.metadata().value(QStringLiteral("filename")).toString();
	const QString messageType = AMessage.metadata().value(QStringLiteral("msgtype")).toString();

	const QString messageFormat = AMessage.metadata().value(QStringLiteral("format")).toString();
	const QString formattedBody = AMessage.metadata().value(QStringLiteral("formatted_body")).toString();
	const QString imageUrl = AMessage.metadata().value(QStringLiteral("url"),
		AMessage.metadata().value(QStringLiteral("image_url"))).toString();
	const QImage decodedImage = AMessage.metadata().value(QStringLiteral("decoded_image")).value<QImage>();
	const QString resourceUrl = AMessage.metadata().value(QStringLiteral("image_resource_url")).toString();
	if (body.isEmpty() && messageType != QStringLiteral("m.image"))
		return;
	const QString replyEventId = AMessage.metadata().value(
		QStringLiteral("reply_to_event_id")).toString();
	const QString historyKey = AMessaging->streamId() + QChar('\n') + AMessage.conversationId();
	const QString replyHtml = MatrixReply::isEventId(replyEventId)
		? buildProtocolReplyPreviewHtml(FProtocolConversationMessages.value(historyKey),
			AWindow, AMessaging, replyEventId, this) : QString();
	const QString bodyForDisplay = !replyHtml.isEmpty()
		? MatrixReply::stripFallbackText(body) : body;
	const QString formattedBodyForDisplay = !replyHtml.isEmpty()
		? MatrixReply::stripFallbackHtml(formattedBody) : formattedBody;

	if (messageType == QStringLiteral("m.text") &&
		messageFormat == QStringLiteral("org.matrix.custom.html") && !formattedBodyForDisplay.isEmpty())
		AWindow->viewWidget()->appendHtml(replyHtml + matrixHighlightMentions(matrixSafeHtml(
			AMessaging->formatEmoticonsForDisplay(formattedBodyForDisplay))), options);
	else if (messageType == QStringLiteral("m.image") && !decodedImage.isNull() && !resourceUrl.isEmpty())
	{
		QTextEdit *view = qobject_cast<QTextEdit *>(AWindow->viewWidget()->styleWidget());
		if (view) {
			view->document()->addResource(QTextDocument::ImageResource, QUrl(resourceUrl), decodedImage);
			notifyImageResourceUpdated(view, QUrl(resourceUrl));
		}
		const QString escapedUrl = resourceUrl.toHtmlEscaped();
		const QString escapedAlt = body.toHtmlEscaped();
		const QString imageHtml = QStringLiteral(
			"<div><img src=\"%1\" alt=\"%2\" "
			"style=\"max-width:50%; max-width:300px; height:auto;\" /></div>")
			.arg(escapedUrl, escapedAlt);
		AWindow->viewWidget()->appendHtml(replyHtml + imageHtml, options);
	}
	else if (messageType == QStringLiteral("m.image") || messageType == QStringLiteral("m.file"))
	{
		const QString filePath = AMessage.metadata().value(QStringLiteral("file_path")).toString();
		if (!filePath.isEmpty()) {
			const QString href = QUrl::fromLocalFile(filePath).toString().toHtmlEscaped();
			AWindow->viewWidget()->appendHtml(replyHtml + QStringLiteral("<a href=\"%1\">%2</a>")
				.arg(href, body.isEmpty() ? tr("Open attachment") : body.toHtmlEscaped()), options);
		} else {
			QUrl requestUrl;
			requestUrl.setScheme(QStringLiteral("vacuum-media"));
			requestUrl.setHost(QStringLiteral("load"));
			QUrlQuery query;
			query.addQueryItem(QStringLiteral("room"), AMessage.conversationId());
			query.addQueryItem(QStringLiteral("event"), AMessage.messageId());
			query.addQueryItem(QStringLiteral("url"), imageUrl);
			query.addQueryItem(QStringLiteral("msgtype"), messageType);
			query.addQueryItem(QStringLiteral("body"), body);
			query.addQueryItem(QStringLiteral("historical"),
				AMessage.metadata().value(QStringLiteral("historical")).toBool()
					? QStringLiteral("true") : QStringLiteral("false"));
			requestUrl.setQuery(query);
			const QString label = messageType == QStringLiteral("m.image")
				? tr("Load image") : tr("Load attachment");
			AWindow->viewWidget()->appendHtml(replyHtml + QStringLiteral("<a href=\"%1\">%2</a>")
				.arg(requestUrl.toString(QUrl::FullyEncoded).toHtmlEscaped(), label), options);
		}
	}
	else
	{
	// Matrix messages are protocol-neutral BasicMessages.  appendText() wraps
	// the text in the legacy Message type and sends it through the XMPP
	// MessageProcessor, which can discard the body because no legacy message
	// type is set.  Render the already selected plain text directly instead.
	const QString displayBody = AMessaging->formatEmoticonsForDisplay(bodyForDisplay);
	const QString escapedBody = displayBody.toHtmlEscaped().replace(QStringLiteral("\n"),
		QStringLiteral("<br/>"));
	AWindow->viewWidget()->appendHtml(replyHtml + matrixHighlightMentions(escapedBody), options);
	}
	if (!AMessage.messageId().isEmpty())
		FProtocolRenderedMessages.insert(prefix + AMessage.messageId());
	FProtocolRenderedMessages.insert(messageKey);
	FProtocolEventMessageIds.insert(eventKey, AMessage.messageId());
	FProtocolMessageDirections.insert(eventKey, options.direction == IMessageContentOptions::DirectionOut);
	FProtocolMessageRelationTypes.insert(eventKey,
		AMessage.metadata().value(QStringLiteral("relation_type")).toString());
	updateProtocolReactionDecoration(AWindow, eventKey, AMessage.messageId());
}

static QString buildProtocolReplyPreviewHtml(const QList<BasicMessage> &history,
	IChatWindow *AWindow, IProtocolMessaging *AMessaging, const QString &AReplyEventId,
	QObject *receiver)
{
	if (!AWindow || !AMessaging || AReplyEventId.isEmpty())
		return QString();
	bool targetFound = false;
	BasicMessage target;
	for (const BasicMessage &candidate : history)
		if (candidate.messageId() == AReplyEventId) {
			target = candidate;
			targetFound = true;
			break;
		}
	const QVariantMap targetMetadata = target.metadata();
	QString senderName = targetFound
		? targetMetadata.value(QStringLiteral("sender_name")).toString()
		: QCoreApplication::translate("ChatMessageHandler", "Unknown sender");
	if (senderName.isEmpty())
		senderName = target.sender();
	if (senderName.isEmpty())
		senderName = QCoreApplication::translate("ChatMessageHandler", "Unknown sender");
	QString excerpt = targetFound ? target.body()
		: QCoreApplication::translate("ChatMessageHandler", "Original message unavailable");
	if (excerpt.isEmpty())
		excerpt = targetMetadata.value(QStringLiteral("body")).toString();
	if (excerpt.isEmpty())
		excerpt = targetMetadata.value(QStringLiteral("filename")).toString();
	if (targetMetadata.value(QStringLiteral("redacted")).toBool())
		excerpt = QCoreApplication::translate("ChatMessageHandler", "Message deleted");
	else if (excerpt.isEmpty() && targetMetadata.value(QStringLiteral("msgtype")).toString() ==
		QStringLiteral("m.image"))
		excerpt = QCoreApplication::translate("ChatMessageHandler", "Image");
	else if (excerpt.isEmpty() && targetMetadata.value(QStringLiteral("msgtype")).toString() ==
		QStringLiteral("m.file"))
		excerpt = QCoreApplication::translate("ChatMessageHandler", "File");
	else if (excerpt.isEmpty())
		excerpt = QCoreApplication::translate("ChatMessageHandler", "Message");
	excerpt = AMessaging->formatEmoticonsForDisplay(excerpt);
	if (excerpt.size() > 180) {
		QTextBoundaryFinder boundary(QTextBoundaryFinder::Grapheme, excerpt);
		boundary.setPosition(180);
		int end = boundary.toPreviousBoundary();
		if (end <= 0)
			end = 180;
		excerpt = excerpt.left(end) + QChar(0x2026);
	}
	QString avatarPath = targetMetadata.value(QStringLiteral("sender_avatar")).toString();
	if (avatarPath.isEmpty() && targetFound)
		avatarPath = AMessaging->userAvatarPath(AWindow->conversationId(), target.sender());
	QTextEdit *view = qobject_cast<QTextEdit *>(AWindow->viewWidget()->styleWidget());
	const QString avatarResource = cacheReplyAvatarResource(view, avatarPath, receiver);
	return MatrixReply::previewHtml(senderName, excerpt, avatarResource);
}

void ChatMessageHandler::renderProtocolHistory(IChatWindow *AWindow, IProtocolMessaging *AMessaging)
{
	if (!AWindow || !AMessaging)
		return;
	const QString historyKey = AMessaging->streamId() + QChar('\n') + AWindow->conversationId();
	FProtocolHistoryLoading.insert(historyKey);
	const QList<BasicMessage> history = AMessaging->conversationHistory(AWindow->conversationId());
	if (history.isEmpty())
		return;
	FProtocolHistoryLoading.remove(historyKey);
	FProtocolHistoryLoaded.insert(historyKey);
	FProtocolConversationMessages.insert(historyKey, history);
	for (const BasicMessage &message : history)
		renderProtocolMessage(AWindow, AMessaging, message);
}

void ChatMessageHandler::sortProtocolMessagesChronologically(QList<BasicMessage> &AMessages) const
{
	std::stable_sort(AMessages.begin(), AMessages.end(), [](const BasicMessage &left,
		const BasicMessage &right) {
		const QDateTime leftTime = left.timestamp();
		const QDateTime rightTime = right.timestamp();
		if (leftTime.isValid() != rightTime.isValid())
			return leftTime.isValid();
		if (!leftTime.isValid())
			return false;
		return leftTime.toMSecsSinceEpoch() < rightTime.toMSecsSinceEpoch();
	});
}

void ChatMessageHandler::rebuildProtocolConversation(IChatWindow *AWindow,
	IProtocolMessaging *AMessaging, const QString &AHistoryKey)
{
	if (!AWindow || !AMessaging)
		return;
	const QString prefix = AMessaging->streamId() + QChar('\n') +
		AWindow->conversationId() + QChar('\n');
	for (auto it = FProtocolRenderedMessages.begin(); it != FProtocolRenderedMessages.end(); )
		it = it->startsWith(prefix) ? FProtocolRenderedMessages.erase(it) : std::next(it);
	for (auto it = FProtocolEventMessageIds.begin(); it != FProtocolEventMessageIds.end(); )
		it = it.key().startsWith(prefix) ? FProtocolEventMessageIds.erase(it) : std::next(it);
	for (auto it = FProtocolMessageDirections.begin(); it != FProtocolMessageDirections.end(); )
		it = it.key().startsWith(prefix) ? FProtocolMessageDirections.erase(it) : std::next(it);
	for (auto it = FProtocolMessageRelationTypes.begin(); it != FProtocolMessageRelationTypes.end(); )
		it = it.key().startsWith(prefix) ? FProtocolMessageRelationTypes.erase(it) : std::next(it);
	for (auto it = FProtocolRedactedMessages.begin(); it != FProtocolRedactedMessages.end(); )
		it = it->startsWith(prefix) ? FProtocolRedactedMessages.erase(it) : std::next(it);
	for (auto it = FProtocolReactionEvents.begin(); it != FProtocolReactionEvents.end(); )
		it = it.key().startsWith(prefix) ? FProtocolReactionEvents.erase(it) : std::next(it);
	for (auto it = FProtocolReactionSenders.begin(); it != FProtocolReactionSenders.end(); )
		it = it.key().startsWith(prefix) ? FProtocolReactionSenders.erase(it) : std::next(it);
	setMessageStyle(AWindow);
	const QList<BasicMessage> history = FProtocolConversationMessages.value(AHistoryKey);
	for (const BasicMessage &message : history)
		renderProtocolMessage(AWindow, AMessaging, message);
}

void ChatMessageHandler::scheduleProtocolConversationRebuild(IChatWindow *AWindow,
	IProtocolMessaging *AMessaging, const QString &AHistoryKey, QObject *AProtocolObject)
{
	if (!AWindow || !AMessaging || AHistoryKey.isEmpty() || !AProtocolObject)
		return;

	const QPointer<QObject> windowObject(AWindow->instance());
	const QPointer<QObject> protocolObject(AProtocolObject);
	if (!windowObject || !protocolObject)
		return;

	FProtocolRebuildScheduler.request(AHistoryKey,
		[this, windowObject, protocolObject, AHistoryKey]() {
			IChatWindow *window = qobject_cast<IChatWindow *>(windowObject.data());
			IProtocolMessaging *messaging = qobject_cast<IProtocolMessaging *>(protocolObject.data());
			if (window && messaging)
				rebuildProtocolConversation(window, messaging, AHistoryKey);
		});
}

void ChatMessageHandler::addProtocolReaction(IChatWindow *AWindow, IProtocolMessaging *AMessaging,
	const BasicMessage &AMessage)
{
	const QString targetEventId = AMessage.metadata().value(QStringLiteral("related_event_id")).toString();
	const QString reactionKey = AMessage.metadata().value(QStringLiteral("reaction_key")).toString();
	const QString senderId = AMessage.sender();
	if (targetEventId.isEmpty() || reactionKey.isEmpty() || senderId.isEmpty() ||
		AMessage.messageId().isEmpty() || AMessage.metadata().value(QStringLiteral("redacted")).toBool())
		return;

	const QString prefix = AMessaging->streamId() + QChar('\n') +
		AMessage.conversationId() + QChar('\n');
	const QString targetKey = prefix + targetEventId;
	const QString targetRelationType = FProtocolMessageRelationTypes.value(targetKey);
	if (FProtocolRedactedMessages.contains(targetKey) || FProtocolReactionEvents.contains(targetKey) ||
		targetRelationType == QStringLiteral("m.annotation") ||
		targetRelationType == QStringLiteral("m.replace"))
		return;
	const QString sourceKey = prefix + AMessage.messageId();
	if (FProtocolReactionEvents.contains(sourceKey))
		return;
	FProtocolReactionEvents.insert(sourceKey,
		ProtocolReactionSource{targetKey, reactionKey, senderId});
	FProtocolReactionSenders[targetKey][reactionKey][senderId].insert(sourceKey);
	updateProtocolReactionDecoration(AWindow, targetKey, targetEventId);
}

void ChatMessageHandler::removeProtocolReaction(IChatWindow *AWindow, const QString &eventPrefix,
	const QString &eventId)
{
	const QString sourceKey = eventPrefix + eventId;
	if (!FProtocolReactionEvents.contains(sourceKey))
		return;
	const ProtocolReactionSource source = FProtocolReactionEvents.take(sourceKey);
	auto targetIt = FProtocolReactionSenders.find(source.targetKey);
	if (targetIt != FProtocolReactionSenders.end()) {
		auto reactionIt = targetIt.value().find(source.reactionKey);
		if (reactionIt != targetIt.value().end()) {
			auto senderIt = reactionIt.value().find(source.senderId);
			if (senderIt != reactionIt.value().end()) {
				senderIt.value().remove(sourceKey);
				if (senderIt.value().isEmpty())
					reactionIt.value().erase(senderIt);
			}
			if (reactionIt.value().isEmpty())
				targetIt.value().erase(reactionIt);
		}
		if (targetIt.value().isEmpty())
			FProtocolReactionSenders.erase(targetIt);
	}
	const QString targetEventId = source.targetKey.mid(eventPrefix.size());
	updateProtocolReactionDecoration(AWindow, source.targetKey, targetEventId);
}

void ChatMessageHandler::updateProtocolReactionDecoration(IChatWindow *AWindow,
	const QString &targetKey, const QString &targetEventId)
{
	if (!AWindow)
		return;
	const QString displayMessageId = FProtocolEventMessageIds.value(targetKey, targetEventId);
	const auto reactions = FProtocolReactionSenders.value(targetKey);
	QStringList chips;
	for (auto reactionIt = reactions.constBegin(); reactionIt != reactions.constEnd(); ++reactionIt) {
		int count = 0;
		for (auto senderIt = reactionIt.value().constBegin(); senderIt != reactionIt.value().constEnd(); ++senderIt)
			if (!senderIt.value().isEmpty())
				++count;
		if (count > 0)
			chips.append(QStringLiteral("<span>%1&nbsp;%2</span>")
				.arg(reactionIt.key().toHtmlEscaped()).arg(count));
	}
	QString html;
	if (!chips.isEmpty()) {
		const bool alignRight = FProtocolMessageDirections.value(targetKey, false);
		const QString reactionChips = chips.join(QStringLiteral("&nbsp;&nbsp;"));
		if (alignRight)
			html = QStringLiteral("<table width=\"100%\" cellpadding=\"0\" cellspacing=\"0\" border=\"0\"><tr>"
				"<td width=\"25%\"></td><td align=\"right\">%1</td><td width=\"5%\"></td></tr></table>")
				.arg(reactionChips);
		else
			html = QStringLiteral("<table width=\"100%\" cellpadding=\"0\" cellspacing=\"0\" border=\"0\"><tr>"
				"<td width=\"36\"></td><td align=\"left\">%1</td><td width=\"20%\"></td></tr></table>")
				.arg(reactionChips);
	}
	AWindow->viewWidget()->setMessageDecoration(displayMessageId,
		QStringLiteral("matrix-reactions"), html);
}

bool ChatMessageHandler::messageCheck(int AOrder, const Message &AMessage, int ADirection)
{
	Q_UNUSED(AOrder); Q_UNUSED(ADirection);
	return AMessage.type()==Message::Chat && !AMessage.body().isEmpty();
}

bool ChatMessageHandler::messageDisplay(const Message &AMessage, int ADirection)
{
	IChatWindow *window = NULL;
	if (ADirection == IMessageProcessor::MessageIn)
		window = AMessage.type()!=Message::Error ? getWindow(AMessage.to(),AMessage.from()) : findWindow(AMessage.to(),AMessage.from());
	else
		window = AMessage.type()!=Message::Error ? getWindow(AMessage.from(),AMessage.to()) : findWindow(AMessage.from(),AMessage.to());
	if (window)
	{
		if (FRecentContacts)
		{
			IRecentItem recentItem;
			recentItem.type = REIT_CONTACT;
			recentItem.streamJid = window->streamJid();
			recentItem.reference = window->contactJid().pBare();
			FRecentContacts->setItemActiveTime(recentItem);
		}
		if (FDestroyTimers.contains(window))
			delete FDestroyTimers.take(window);
		if (FHistoryRequests.values().contains(window))
			FPendingMessages[window].append(AMessage);
		showStyledMessage(window,AMessage);
	}

	return window!=NULL;
}

INotification ChatMessageHandler::messageNotify(INotifications *ANotifications, const Message &AMessage, int ADirection)
{
	INotification notify;
	if (ADirection == IMessageProcessor::MessageIn
		&& !messageNotificationMuted(AMessage.to(), AMessage.from())
		&& !messageNotificationMuted(AMessage.to(), AMessage.to()))
	{
		IChatWindow *window = findWindow(AMessage.to(),AMessage.from());
		if (window && !window->isActiveTabPage())
		{
			notify.kinds = ANotifications->enabledTypeNotificationKinds(NNT_CHAT_MESSAGE);
			if (notify.kinds > 0)
			{
				QIcon icon = IconStorage::staticStorage(RSR_STORAGE_MENUICONS)->getIcon(MNI_CHAT_MHANDLER_MESSAGE);
				QString name = ANotifications->contactName(AMessage.to(),AMessage.from());

				notify.typeId = NNT_CHAT_MESSAGE;
				notify.data.insert(NDR_ICON,icon);
				notify.data.insert(NDR_TOOLTIP,tr("Message from %1").arg(name));
				notify.data.insert(NDR_STREAM_JID,AMessage.to());
				notify.data.insert(NDR_CONTACT_JID,AMessage.from());
				notify.data.insert(NDR_ROSTER_ORDER,RNO_CHATMESSAGE);
				notify.data.insert(NDR_ROSTER_FLAGS,IRostersNotify::Blink|IRostersNotify::AllwaysVisible|IRostersNotify::HookClicks|IRostersNotify::BlinkStatusIcon);
				notify.data.insert(NDR_ROSTER_CREATE_INDEX,true);
				notify.data.insert(NDR_POPUP_IMAGE,ANotifications->contactAvatar(AMessage.from()));
				notify.data.insert(NDR_POPUP_CAPTION, tr("Message received"));
				notify.data.insert(NDR_POPUP_TITLE,name);
				notify.data.insert(NDR_SOUND_FILE,SDF_CHAT_MHANDLER_MESSAGE);

				notify.data.insert(NDR_ALERT_WIDGET,(qint64)window->instance());
				notify.data.insert(NDR_TABPAGE_WIDGET,(qint64)window->instance());
				notify.data.insert(NDR_TABPAGE_PRIORITY,TPNP_NEW_MESSAGE);
				notify.data.insert(NDR_TABPAGE_ICONBLINK,true);
				notify.data.insert(NDR_SHOWMINIMIZED_WIDGET,(qint64)window->instance());

				if (FMessageProcessor)
				{
					QTextDocument doc;
					FMessageProcessor->messageToText(&doc,AMessage);
					notify.data.insert(NDR_POPUP_HTML,TextManager::getDocumentBody(doc));
				}
				else
				{
					notify.data.insert(NDR_POPUP_HTML,AMessage.body().toHtmlEscaped());
				}

				FNotifiedMessages.insertMulti(window, AMessage.data(MDR_MESSAGE_ID).toInt());
			}
		}
	}
	return notify;
}

bool ChatMessageHandler::messageShowWindow(int AMessageId)
{
	IChatWindow *window = FNotifiedMessages.key(AMessageId);
	if (window)
	{
		window->showTabPage();
		return true;
	}
	return false;
}

bool ChatMessageHandler::messageShowWindow(int AOrder, const Jid &AStreamJid, const Jid &AContactJid, Message::MessageType AType, int AShowMode)
{
	Q_UNUSED(AOrder);
	if (AType == Message::Chat)
	{
		IChatWindow *window = getWindow(AStreamJid,AContactJid);
		if (window)
		{
			const QString historyKey = AStreamJid.bare() + QChar('\n') + AContactJid.bare();
			if (!FConversationHistoryLoaded.contains(historyKey) && FMessageProcessor)
			{
				FConversationHistoryLoaded.insert(historyKey);
				FMessageProcessor->displayConversationHistory(AStreamJid,AContactJid);
			}
			if (AShowMode == IMessageHandler::SM_ASSIGN)
				window->assignTabPage();
			else if (AShowMode == IMessageHandler::SM_SHOW)
				window->showTabPage();
			else if (AShowMode == IMessageHandler::SM_MINIMIZED)
				window->showMinimizedTabPage();
			return true;
		}
	}
	return false;
}

IChatWindow *ChatMessageHandler::getWindow(const Jid &AStreamJid, const Jid &AContactJid)
{
	IChatWindow *window = NULL;
	if (AStreamJid.isValid() && AContactJid.isValid())
	{
		window = findSubstituteWindow(AStreamJid,AContactJid);
		if (!window)
		{
			window = FMessageWidgets->getChatWindow(AStreamJid,AContactJid);
			if (window)
			{
				window->infoWidget()->autoUpdateFields();
				window->setTabPageNotifier(FMessageWidgets->newTabPageNotifier(window));

				connect(window->instance(),SIGNAL(messageReady()),SLOT(onMessageReady()));
				connect(window->instance(),SIGNAL(tabPageActivated()),SLOT(onWindowActivated()));
				connect(window->instance(),SIGNAL(tabPageClosed()),SLOT(onWindowClosed()));
				connect(window->instance(),SIGNAL(tabPageDestroyed()),SLOT(onWindowDestroyed()));
				connect(window->tabPageNotifier()->instance(),SIGNAL(activeNotifyChanged(int)),this,SLOT(onWindowNotifierActiveNotifyChanged(int)));
				connect(window->infoWidget()->instance(),SIGNAL(fieldChanged(int, const QVariant &)),SLOT(onWindowInfoFieldChanged(int, const QVariant &)), Qt::QueuedConnection);

				FWindows.append(window);
				FWindowStatus[window].createTime = QDateTime::currentDateTime();
				updateWindow(window);
				setMessageStyle(window);

				Action *clearAction = new Action(window->instance());
				clearAction->setText(tr("Clear Chat Window"));
				clearAction->setIcon(RSR_STORAGE_MENUICONS,MNI_CHAT_MHANDLER_CLEAR_CHAT);
				clearAction->setShortcutId(SCT_MESSAGEWINDOWS_CHAT_CLEARWINDOW);
				connect(clearAction,SIGNAL(triggered(bool)),SLOT(onClearWindowAction(bool)));
				window->toolBarWidget()->toolBarChanger()->insertAction(clearAction, TBG_MWTBW_CLEAR_WINDOW);
				setupFileTransferAction(window);

				if (FRostersView && FRostersModel)
				{
					UserContextMenu *menu = new UserContextMenu(FRostersModel,FRostersView,window);
					menu->menuAction()->setIcon(RSR_STORAGE_MENUICONS, MNI_CHAT_MHANDLER_USER_MENU);
					QToolButton *button = window->toolBarWidget()->toolBarChanger()->insertAction(menu->menuAction(),TBG_CWTBW_USER_TOOLS);
					button->setPopupMode(QToolButton::InstantPopup);
				}

				showHistory(window);
			}
			else
			{
				window = findWindow(AStreamJid,AContactJid);
			}
		}
		else if(!AContactJid.resource().isEmpty() && window->contactJid()!=AContactJid)
		{
			window->setContactJid(AContactJid);
		}
	}
	return window;
}

IChatWindow *ChatMessageHandler::findWindow(const Jid &AStreamJid, const Jid &AContactJid) const
{
	foreach(IChatWindow *window, FWindows)
		if (window->streamJid()==AStreamJid && window->contactJid()==AContactJid)
			return window;
	return NULL;
}

IChatWindow *ChatMessageHandler::findSubstituteWindow(const Jid &AStreamJid, const Jid &AContactJid) const
{
	IChatWindow *fullWindow = NULL;
	IChatWindow *bareWindow = NULL;
	IChatWindow *offlineWindow = NULL;
	IPresence *presence = FPresencePlugin!=NULL ? FPresencePlugin->findPresence(AStreamJid) : NULL;

	int maxResDiff = -1;
	foreach(IChatWindow *window, FWindows)
	{
		if (window->streamJid() == AStreamJid)
		{
			if (window->contactJid() == AContactJid)
			{
				fullWindow = window;
				break;
			}
			else if(presence && !bareWindow && (window->contactJid() && AContactJid))
			{
				IPresenceItem pitem = presence->presenceItem(window->contactJid());
				if (pitem.show==IPresence::Offline || pitem.show==IPresence::Error)
				{
					if (window->contactJid() == AContactJid.bare())
					{
						bareWindow = window;
					}
					else
					{
						int resDiff = 0;
						QString contactRes = AContactJid.resource();
						QString offlineRes = window->contactJid().resource();
						while(offlineRes.size()>resDiff && contactRes.size()>resDiff && offlineRes.at(resDiff)==contactRes.at(resDiff))
						{
							resDiff++;
						}
						if (maxResDiff < resDiff)
						{
							maxResDiff = resDiff;
							offlineWindow = window;
						}
					}
				}
			}
		}
	}

	if (fullWindow)
		return fullWindow;
	else if(bareWindow)
		return bareWindow;
	else if(offlineWindow)
		return offlineWindow;

	return NULL;
}

void ChatMessageHandler::updateWindow(IChatWindow *AWindow)
{
	QIcon icon;
	if (AWindow->tabPageNotifier() && AWindow->tabPageNotifier()->activeNotify()>0)
		icon = AWindow->tabPageNotifier()->notifyById(AWindow->tabPageNotifier()->activeNotify()).icon;
	if (FStatusIcons && icon.isNull() && AWindow->conversationId().isEmpty())
		icon = FStatusIcons->iconByJid(AWindow->streamJid(),AWindow->contactJid());

	QString contactName = AWindow->infoWidget()->field(IInfoWidget::ContactName).toString();
	AWindow->updateWindow(icon,contactName,tr("%1 - Chat").arg(contactName),QString());
}

void ChatMessageHandler::setupFileTransferAction(IChatWindow *AWindow, IProtocolMessaging *AMessaging)
{
	if (!AWindow || !AWindow->toolBarWidget())
		return;
	Action *fileAction = new Action(AWindow->instance());
	fileAction->setText(QString());
	fileAction->setToolTip(tr("Send file..."));
	fileAction->setIcon(RSR_STORAGE_MENUICONS, MNI_FILETRANSFER_SEND);
	connect(fileAction, &QAction::triggered, AWindow->instance(), [this, AWindow, AMessaging]() {
		const QString filePath = QFileDialog::getOpenFileName(AWindow->instance(), QObject::tr("Select file"));
		if (filePath.isEmpty())
			return;
		const QFileInfo info(filePath);
		if (!AMessaging) {
			if (FFileTransfer && FFileTransfer->isSupported(AWindow->streamJid(), AWindow->contactJid()))
				FFileTransfer->sendFile(AWindow->streamJid(), AWindow->contactJid(), filePath);
			return;
		}
		const QString mimeType = QMimeDatabase().mimeTypeForFile(info).name();
		BasicMessage message(QString(), AWindow->conversationId(), QString(), QString(),
			info.fileName(), QDateTime::currentDateTimeUtc(), QStringLiteral("matrix"), BasicMessage::Outgoing);
		QVariantMap metadata;
		metadata.insert(QStringLiteral("file_path"), filePath);
		metadata.insert(QStringLiteral("mimetype"), mimeType);
		metadata.insert(QStringLiteral("msgtype"), mimeType.startsWith(QStringLiteral("image/"))
			? QStringLiteral("m.image") : QStringLiteral("m.file"));
		message.setMetadata(metadata);
		AMessaging->sendMessage(message);
	});
	AWindow->toolBarWidget()->toolBarChanger()->insertAction(fileAction, TBG_DEFAULT);
}

void ChatMessageHandler::setupRoomSidebar(IChatWindow *AWindow, IProtocolMessaging *AMessaging)
{
	IProtocolRoster *roster = nullptr;
	if (FPluginManager) {
		for (IPlugin *plugin : FPluginManager->pluginInterface("IProtocolRoster")) {
			IProtocolRoster *candidate = qobject_cast<IProtocolRoster *>(plugin->instance());
			if (candidate && candidate->streamId() == AMessaging->streamId()) {
				roster = candidate;
				break;
			}
		}
	}
	if (!AWindow || !AMessaging || !roster) {
		if (AWindow) {
			if (QObject *windowObject = AWindow->instance())
				for (QWidget *sidebar : windowObject->findChildren<QWidget *>(QStringLiteral("matrixRoomSidebar")))
					sidebar->setProperty("matrixSidebarCurrent", false);
			AWindow->setSidebarWidget(nullptr);
		}
		return;
	}
	AMessaging->loadConversationAvatars(AWindow->conversationId());
	const ProtocolRoom currentRoom = roster->room(AWindow->conversationId());
	if (currentRoom.id.isEmpty())
		return;
	const QString accountId = AMessaging->streamId();
	const QByteArray memberSnapshot = RoomSidebarState::snapshot(currentRoom, accountId);
	QWidget *existingSidebar = nullptr;
	if (QObject *windowObject = AWindow->instance())
		for (QWidget *sidebar : windowObject->findChildren<QWidget *>(QStringLiteral("matrixRoomSidebar")))
			if (sidebar->property("matrixSidebarCurrent").toBool()) {
				existingSidebar = sidebar;
				break;
			}
	const bool sameSidebarIdentity = existingSidebar &&
		existingSidebar->property("matrixRoomId").toString() == currentRoom.id &&
		existingSidebar->property("matrixAccountId").toString() == accountId;
	if (RoomSidebarState::canReuseSidebar(
		existingSidebar ? existingSidebar->property("matrixAccountId").toString() : QString(),
		existingSidebar ? existingSidebar->property("matrixRoomId").toString() : QString(),
		existingSidebar ? existingSidebar->property("matrixMemberSnapshot").toByteArray() : QByteArray(),
		accountId, currentRoom.id, memberSnapshot))
		return;
	QHash<QString, QPixmap> loadedMemberAvatars;
	if (sameSidebarIdentity)
		for (QLabel *avatar : existingSidebar->findChildren<QLabel *>(QStringLiteral("matrixMemberAvatar"))) {
			if (!avatar->property("matrixAvatarHasLoaded").toBool())
				continue;
			const QPixmap pixmap = avatar->pixmap();
			const QString userId = avatar->property("matrixUserId").toString();
			if (!userId.isEmpty() && !pixmap.isNull())
				loadedMemberAvatars.insert(userId, pixmap);
		}
	if (existingSidebar)
		existingSidebar->setProperty("matrixSidebarCurrent", false);
	QWidget *sidebar = new QWidget;
	QVBoxLayout *layout = new QVBoxLayout(sidebar);
	layout->setContentsMargins(12, 12, 12, 12);
	layout->setSpacing(8);
	sidebar->setObjectName(QStringLiteral("matrixRoomSidebar"));
	sidebar->setProperty("matrixRoomId", currentRoom.id);
	sidebar->setProperty("matrixAccountId", accountId);
	sidebar->setProperty("matrixMemberSnapshot", memberSnapshot);
	sidebar->setProperty("matrixSidebarCurrent", true);
	QLabel *title = new QLabel(currentRoom.name.isEmpty() ? currentRoom.id : currentRoom.name, sidebar);
	title->setWordWrap(true);
	title->setStyleSheet(QStringLiteral("font-size:16px; font-weight:600;"));
	layout->addWidget(title);
	const QString encryptionText = currentRoom.isEncrypted
		? tr("End-to-end encrypted") : tr("Not end-to-end encrypted");
	QWidget *encryptionRow = new QWidget(sidebar);
	QHBoxLayout *encryptionLayout = new QHBoxLayout(encryptionRow);
	encryptionLayout->setContentsMargins(0, 0, 0, 0);
	encryptionLayout->setSpacing(6);
	QLabel *encryptionIcon = new QLabel(encryptionRow);
	if (currentRoom.isEncrypted) {
		encryptionIcon->setPixmap(IconStorage::staticStorage(RSR_STORAGE_MENUICONS)
			->getIcon(MNI_CONNECTION_ENCRYPTED).pixmap(16, 16));
	} else {
		encryptionIcon->hide();
	}
	encryptionIcon->setAccessibleName(encryptionText);
	encryptionLayout->addWidget(encryptionIcon);
	QLabel *encryptionStatus = new QLabel(encryptionText, encryptionRow);
	encryptionStatus->setWordWrap(true);
	encryptionLayout->addWidget(encryptionStatus, 1);
	layout->addWidget(encryptionRow);
	QLabel *people = new QLabel(tr("People (%1)").arg(currentRoom.members.size()), sidebar);
	people->setStyleSheet(QStringLiteral("color:#7b8794; margin-top:8px;"));
	layout->addWidget(people);
	QScrollArea *scroll = new QScrollArea(sidebar);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	QWidget *memberList = new QWidget(scroll);
	QVBoxLayout *memberLayout = new QVBoxLayout(memberList);
	memberLayout->setContentsMargins(0, 4, 0, 4);
	memberLayout->setSpacing(6);
	for (const ProtocolRosterEntry &member : currentRoom.members) {
		QPushButton *row = new QPushButton(memberList);
		row->setToolTip(member.id);
		row->setCursor(Qt::PointingHandCursor);
		row->setFlat(true);
		QHBoxLayout *rowLayout = new QHBoxLayout(row);
		rowLayout->setContentsMargins(8, 4, 8, 4);
		rowLayout->setSpacing(8);
		QLabel *avatar = new QLabel(row);
		RoomSidebarState::configureMemberAvatarLabel(avatar);
		avatar->setAlignment(Qt::AlignCenter);
		avatar->setObjectName(QStringLiteral("matrixMemberAvatar"));
		avatar->setProperty("matrixAvatarKey", AMessaging->userAvatarKey(currentRoom.id, member.id));
		avatar->setProperty("matrixAccountId", AMessaging->streamId());
		avatar->setProperty("matrixRoomId", currentRoom.id);
		avatar->setProperty("matrixUserId", member.id);
		const QString cachedPath = AMessaging->userAvatarPath(currentRoom.id, member.id);
		if (!cachedPath.isEmpty())
			loadImagePixmapAsync(avatar, cachedPath, avatar->size());
		const auto loadedAvatar = loadedMemberAvatars.constFind(member.id);
		switch (RoomSidebarState::avatarAction(loadedAvatar != loadedMemberAvatars.cend(),
			!cachedPath.isEmpty())) {
		case RoomSidebarState::AvatarAction::ReuseLoaded:
			avatar->setPixmap(RoomSidebarState::avatarPixmapForDisplay(
				loadedAvatar.value(), avatar->size()));
			avatar->setProperty("matrixAvatarHasLoaded", true);
			avatar->setProperty("matrixAvatarIsPlaceholder", false);
			break;
		case RoomSidebarState::AvatarAction::LoadCached:
			avatar->setProperty("matrixAvatarHasLoaded", false);
			avatar->setProperty("matrixAvatarIsPlaceholder", false);
			break;
		case RoomSidebarState::AvatarAction::Placeholder:
			QImage placeholder(21, 21, QImage::Format_ARGB32_Premultiplied);
			placeholder.fill(QColor(QStringLiteral("#c8d0d9")));
			avatar->setPixmap(QPixmap::fromImage(
				RoundedAvatar::roundImageScaled(placeholder, avatar->size())));
			avatar->setProperty("matrixAvatarHasLoaded", false);
			avatar->setProperty("matrixAvatarIsPlaceholder", true);
			break;
		}
		row->setProperty("matrixUserId", member.id);
		rowLayout->addWidget(avatar);
		QString verificationLabel;
		if (member.hasVerificationState) {
			QLabel *verification = new QLabel(row);
			verification->setFixedSize(8, 8);
			verificationLabel = member.isVerified ? tr("Verified") : tr("Unverified");
			verification->setAccessibleName(verificationLabel);
			verification->setToolTip(verificationLabel);
			const QString verificationColor = member.isVerified
				? QStringLiteral("#2e9d63") : QStringLiteral("#d9534f");
			verification->setStyleSheet(QStringLiteral("background-color:%1; border-radius:4px;")
				.arg(verificationColor));
			rowLayout->addWidget(verification);
		}
		QLabel *name = new QLabel(member.name.isEmpty() ? member.id : member.name, row);
		rowLayout->addWidget(name, 1);
		row->setAccessibleName(verificationLabel.isEmpty() ? name->text()
			: name->text() + QStringLiteral(", ") + verificationLabel);
		row->setStyleSheet(QStringLiteral("text-align:left; padding:4px;"));
		connect(row, &QPushButton::clicked, sidebar, [this, AWindow, AMessaging, currentRoom, member]() {
			showRoomMemberProfile(AWindow->instance(), AMessaging, currentRoom, member);
		});
		memberLayout->addWidget(row);
	}
	memberLayout->addStretch(1);
	scroll->setWidget(memberList);
	layout->addWidget(scroll, 1);
	auto requestVisibleMemberAvatars = [scroll, memberList, AMessaging, roomId = currentRoom.id]() {
		const int visibleTop = scroll->verticalScrollBar()->value();
		const int visibleBottom = visibleTop + scroll->viewport()->height();
		for (QPushButton *row : memberList->findChildren<QPushButton *>()) {
			const int rowTop = row->y();
			const int rowBottom = rowTop + row->height();
			if (rowBottom < visibleTop || rowTop > visibleBottom ||
				row->property("matrixAvatarRequested").toBool())
				continue;
			row->setProperty("matrixAvatarRequested", true);
			AMessaging->loadUserAvatar(roomId, row->property("matrixUserId").toString());
		}
	};
	connect(scroll->verticalScrollBar(), &QScrollBar::valueChanged, sidebar,
		[requestVisibleMemberAvatars](int) { requestVisibleMemberAvatars(); });
	QTimer::singleShot(0, sidebar, requestVisibleMemberAvatars);

	// Show the invite action only for protocols that support room invitations.
	if (AMessaging->supportsRoomInvites()) {
		QPushButton *inviteButton = new QPushButton(sidebar);
		inviteButton->setCursor(Qt::PointingHandCursor);
		inviteButton->setFlat(true);
		const QIcon inviteIcon = IconStorage::staticStorage(RSR_STORAGE_MENUICONS)->getIcon(MNI_MUC_INVITE);
		if (!inviteIcon.isNull()) {
			inviteButton->setIcon(inviteIcon);
		} else {
			QPixmap placeholder(16, 16);
			placeholder.fill(Qt::transparent);
			QPainter painter(&placeholder);
			painter.setPen(QColor("#58a6ff"));
			painter.setFont(QFont("Sans Serif", 12, QFont::Bold));
			painter.drawText(placeholder.rect(), Qt::AlignCenter, "+");
			inviteButton->setIcon(QIcon(placeholder));
		}
		inviteButton->setStyleSheet(
			QStringLiteral("QPushButton { padding: 4px 8px; border: none; background: transparent; color: #58a6ff; text-align: left; font-size: 12px; } "
			               "QPushButton:hover { background-color: #f0f6fc; border-radius: 3px; }")
		);
		connect(inviteButton, &QPushButton::clicked, sidebar, [this, AMessaging, AWindow, currentRoom]() {
			showRoomInviteDialog(AWindow->instance(), AMessaging, currentRoom);
		});
		layout->addWidget(inviteButton);
	}

	AWindow->setSidebarWidget(sidebar);
}

void ChatMessageHandler::showRoomMemberProfile(QWidget *AParent, IProtocolMessaging *AMessaging,
	const ProtocolRoom &ARoom, const ProtocolRosterEntry &AMember)
{
	AMessaging->loadUserAvatar(ARoom.id, AMember.id);
	QDialog *dialog = new QDialog(AParent);
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->setWindowTitle(tr("Show Profile"));
	QVBoxLayout *layout = new QVBoxLayout(dialog);
	QLabel *name = new QLabel(AMember.name.isEmpty() ? AMember.id : AMember.name, dialog);
	name->setStyleSheet(QStringLiteral("font-size:16px; font-weight:600;"));
	layout->addWidget(name);
	const QString avatarPath = AMessaging->userAvatarPath(ARoom.id, AMember.id);
	if (!avatarPath.isEmpty()) {
		QLabel *avatar = new QLabel(dialog);
		avatar->setFixedSize(96, 96);
		avatar->setObjectName(QStringLiteral("matrixMemberProfileAvatar"));
		avatar->setProperty("matrixAvatarKey", AMessaging->userAvatarKey(ARoom.id, AMember.id));
		avatar->setProperty("matrixAccountId", AMessaging->streamId());
		avatar->setProperty("matrixRoomId", ARoom.id);
		avatar->setProperty("matrixUserId", AMember.id);
		loadImagePixmapAsync(avatar, avatarPath, QSize(96, 96));
		avatar->setAlignment(Qt::AlignCenter);
		layout->addWidget(avatar);
	}
	QLabel *id = new QLabel(tr("Matrix ID: %1").arg(AMember.id), dialog);
	id->setTextInteractionFlags(Qt::TextSelectableByMouse);
	layout->addWidget(id);
	QLabel *presence = new QLabel(tr("Presence: %1").arg(AMember.presence), dialog);
	layout->addWidget(presence);
	QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
	connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
	layout->addWidget(buttons);
	dialog->resize(320, 260);
	dialog->show();
}

void ChatMessageHandler::setupProtocolWindow(IChatWindow *AWindow, IProtocolMessaging *AMessaging)
{
	if (!AWindow || !AMessaging || FWindows.contains(AWindow))
		return;
	AWindow->infoWidget()->autoUpdateFields();
	const QString displayName = AMessaging->conversationDisplayName(AWindow->conversationId());
	if (!displayName.isEmpty())
		AWindow->infoWidget()->setField(IInfoWidget::ContactName, displayName);
	const QString avatarPath = AMessaging->conversationAvatarPath(AWindow->conversationId());
	if (!avatarPath.isEmpty())
		AWindow->infoWidget()->setField(IInfoWidget::ContactAvatar, avatarPath);
	setupRoomSidebar(AWindow, AMessaging);

	// Install ESC-to-cancel-reply filter on the edit widget for Matrix windows
	if (AMessaging && AMessaging->protocol() == QStringLiteral("matrix")) {
		IEditWidget *editWidget = AWindow->editWidget();
		if (editWidget && editWidget->textEdit()) {
			QTextEdit *editor = editWidget->textEdit();
			if (!FReplyEscFilter) {
				FReplyEscFilter = new QObject(editor);
			}
			editor->installEventFilter(FReplyEscFilter);
			connect(FReplyEscFilter, SIGNAL(destroyed(QObject*)), SLOT(onReplyEscFilterDestroyed()));
		}
	}

	AWindow->setTabPageNotifier(FMessageWidgets->newTabPageNotifier(AWindow));
	connect(AWindow->instance(),SIGNAL(messageReady()),SLOT(onMessageReady()));
	connect(AWindow->instance(),SIGNAL(tabPageActivated()),SLOT(onWindowActivated()));
	connect(AWindow->instance(),SIGNAL(tabPageClosed()),SLOT(onWindowClosed()));
	connect(AWindow->instance(),SIGNAL(tabPageDestroyed()),SLOT(onWindowDestroyed()));
	connect(AWindow->tabPageNotifier()->instance(),SIGNAL(activeNotifyChanged(int)),this,SLOT(onWindowNotifierActiveNotifyChanged(int)));
	connect(AWindow->infoWidget()->instance(),SIGNAL(fieldChanged(int, const QVariant &)),SLOT(onWindowInfoFieldChanged(int, const QVariant &)), Qt::QueuedConnection);
	setupFileTransferAction(AWindow, AMessaging);
	connect(AWindow->viewWidget()->instance(), SIGNAL(urlClicked(const QUrl &)),
		this, SLOT(onProtocolUrlClicked(const QUrl &)), Qt::UniqueConnection);
	connect(AWindow->viewWidget()->instance(),
		SIGNAL(viewContextMenu(const QPoint &, const QTextDocumentFragment &, Menu *)),
		this, SLOT(onProtocolViewContextMenu(const QPoint &, const QTextDocumentFragment &, Menu *)),
		Qt::UniqueConnection);
	FWindows.append(AWindow);
	FWindowStatus[AWindow].createTime = QDateTime::currentDateTime();
	setMessageStyle(AWindow);
	updateWindow(AWindow);
}

void ChatMessageHandler::removeNotifiedMessages(IChatWindow *AWindow)
{
	if (FNotifiedMessages.contains(AWindow))
	{
		foreach(int messageId, FNotifiedMessages.values(AWindow))
			FMessageProcessor->removeMessageNotify(messageId);
		FNotifiedMessages.remove(AWindow);
	}
}

void ChatMessageHandler::showHistory(IChatWindow *AWindow)
{
	if (FMessageArchiver && Options::node(OPV_MESSAGES_LOAD_HISTORY).value().toBool() && !FHistoryRequests.values().contains(AWindow))
	{
		WindowStatus &wstatus = FWindowStatus[AWindow];

		IArchiveRequest request;
		request.with = AWindow->contactJid().bare();
		request.exactmatch = request.with.node().isEmpty();
		request.order = Qt::DescendingOrder;
		if (wstatus.createTime.secsTo(QDateTime::currentDateTime()) > HISTORY_TIME_DELTA)
			request.start = wstatus.startTime.isValid() ? wstatus.startTime : wstatus.createTime;
		else
			request.maxItems = HISTORY_MESSAGES;
		request.end = QDateTime::currentDateTime();

		QString reqId = FMessageArchiver->loadMessages(AWindow->streamJid(),request);
		if (!reqId.isEmpty())
		{
			showStyledStatus(AWindow,tr("Loading history..."),true);
			FHistoryRequests.insert(reqId,AWindow);
		}
	}
}

void ChatMessageHandler::setMessageStyle(IChatWindow *AWindow)
{
	IMessageStyleOptions soptions = FMessageStyles->styleOptions(Message::Chat);
	if (AWindow->viewWidget()->messageStyle()==NULL || !AWindow->viewWidget()->messageStyle()->changeOptions(AWindow->viewWidget()->styleWidget(),soptions,true))
	{
		IMessageStyle *style = FMessageStyles->styleForOptions(soptions);
		AWindow->viewWidget()->setMessageStyle(style,soptions);
	}
	FWindowStatus[AWindow].lastDateSeparator = QDate();
}

void ChatMessageHandler::fillContentOptions(IChatWindow *AWindow, IMessageContentOptions &AOptions) const
{
	if (AOptions.direction == IMessageContentOptions::DirectionIn)
	{
		AOptions.senderId = AWindow->contactJid().full();
		AOptions.senderName = FMessageStyles->contactName(AWindow->streamJid(),AWindow->contactJid()).toHtmlEscaped();
		AOptions.senderAvatar = FMessageStyles->contactAvatar(AWindow->contactJid());
		AOptions.senderIcon = FMessageStyles->contactIcon(AWindow->streamJid(),AWindow->contactJid());
		AOptions.senderColor = "blue";
	}
	else
	{
		AOptions.senderId = AWindow->streamJid().full();
		if (AWindow->streamJid() && AWindow->contactJid())
			AOptions.senderName = (!AWindow->streamJid().resource().isEmpty() ? AWindow->streamJid().resource() : AWindow->streamJid().uNode()).toHtmlEscaped();
		else
			AOptions.senderName = FMessageStyles->contactName(AWindow->streamJid()).toHtmlEscaped();
		AOptions.senderAvatar = FMessageStyles->contactAvatar(AWindow->streamJid());
		AOptions.senderIcon = FMessageStyles->contactIcon(AWindow->streamJid());
		AOptions.senderColor = "red";
	}
}

void ChatMessageHandler::showDateSeparator(IChatWindow *AWindow, const QDateTime &ADateTime)
{
	if (Options::node(OPV_MESSAGES_SHOWDATESEPARATORS).value().toBool())
	{
		QDate sepDate = ADateTime.date();
		WindowStatus &wstatus = FWindowStatus[AWindow];
		if (FMessageStyles && sepDate.isValid() && wstatus.lastDateSeparator!=sepDate)
		{
			IMessageContentOptions options;
			options.kind = IMessageContentOptions::KindStatus;
			if (wstatus.createTime > ADateTime)
				options.type |= IMessageContentOptions::TypeHistory;
			options.status = IMessageContentOptions::StatusDateSeparator;
			options.direction = IMessageContentOptions::DirectionIn;
			options.time.setDate(sepDate);
			options.time.setTime(QTime(0,0));
			options.timeFormat = " ";
			wstatus.lastDateSeparator = sepDate;
			AWindow->viewWidget()->appendText(FMessageStyles->dateSeparator(sepDate),options);
		}
	}
}

void ChatMessageHandler::showStyledStatus(IChatWindow *AWindow, const QString &AMessage, bool ADontSave, const QDateTime &ATime)
{
	IMessageContentOptions options;
	options.kind = IMessageContentOptions::KindStatus;
	options.direction = IMessageContentOptions::DirectionIn;

	options.time = ATime;
	if (Options::node(OPV_MESSAGES_SHOWDATESEPARATORS).value().toBool())
		options.timeFormat = FMessageStyles->timeFormat(options.time,options.time);
	else
		options.timeFormat = FMessageStyles->timeFormat(options.time);

	if (!ADontSave && FMessageArchiver && Options::node(OPV_MESSAGES_ARCHIVESTATUS).value().toBool())
		FMessageArchiver->saveNote(AWindow->streamJid(), AWindow->contactJid(), AMessage);

	fillContentOptions(AWindow,options);
	showDateSeparator(AWindow,options.time);
	AWindow->viewWidget()->appendText(AMessage,options);
}

void ChatMessageHandler::showStyledMessage(IChatWindow *AWindow, const Message &AMessage)
{
	IMessageContentOptions options;
	options.kind = IMessageContentOptions::KindMessage;

	options.time = AMessage.dateTime();
	if (Options::node(OPV_MESSAGES_SHOWDATESEPARATORS).value().toBool())
		options.timeFormat = FMessageStyles->timeFormat(options.time,options.time);
	else
		options.timeFormat = FMessageStyles->timeFormat(options.time);

	if (options.time.secsTo(FWindowStatus.value(AWindow).createTime)>HISTORY_TIME_DELTA)
		options.type |= IMessageContentOptions::TypeHistory;

	if (AWindow->streamJid() && AWindow->contactJid() ? AWindow->contactJid()!=AMessage.to() : !(AWindow->contactJid() && AMessage.to()))
		options.direction = IMessageContentOptions::DirectionIn;
	else
		options.direction = IMessageContentOptions::DirectionOut;

	fillContentOptions(AWindow,options);
	showDateSeparator(AWindow,options.time);
	AWindow->viewWidget()->appendMessage(AMessage,options);
}

bool ChatMessageHandler::isSelectionAccepted(const QList<IRosterIndex *> &ASelected) const
{
	static const QList<int> chatDialogTypes = QList<int>() << RIT_CONTACT << RIT_AGENT << RIT_MY_RESOURCE;
	if (!ASelected.isEmpty())
	{
		foreach(IRosterIndex *index, ASelected)
		{
			int indexType = index->type();
			if (!chatDialogTypes.contains(indexType))
				return false;
		}
		return true;
	}
	return false;
}

void ChatMessageHandler::onProtocolUrlClicked(const QUrl &AUrl)
{
	QObject *source = sender();
	IChatWindow *window = nullptr;
	for (IChatWindow *candidate : FWindows)
		if (candidate && candidate->viewWidget() && candidate->viewWidget()->instance() == source) {
			window = candidate;
			break;
		}
	if (!window)
		return;
	if (AUrl.scheme() == QStringLiteral("vacuum-media")) {
		const QUrlQuery query(AUrl);
		const QString roomId = query.queryItemValue(QStringLiteral("room"));
		const QString eventId = query.queryItemValue(QStringLiteral("event"));
		IProtocolMessaging *messaging = nullptr;
		for (IProtocolMessaging *candidate : FProtocolMessaging)
			if (candidate && candidate->streamId() == window->accountId() &&
				candidate->protocol() == QStringLiteral("matrix")) {
				messaging = candidate;
				break;
			}
		if (!messaging || roomId != window->conversationId() || eventId.isEmpty())
			return;
		BasicMessage message(eventId, roomId, QString(), QString(),
			query.queryItemValue(QStringLiteral("body")), QDateTime::currentDateTimeUtc(),
			QStringLiteral("matrix"), BasicMessage::Incoming);
		QVariantMap metadata;
		metadata.insert(QStringLiteral("url"), query.queryItemValue(QStringLiteral("url")));
		metadata.insert(QStringLiteral("msgtype"), query.queryItemValue(QStringLiteral("msgtype")));
		metadata.insert(QStringLiteral("historical"),
			query.queryItemValue(QStringLiteral("historical")) == QStringLiteral("true"));
		message.setMetadata(metadata);
		messaging->loadConversationMedia(message);
		return;
	}
	if (AUrl.scheme() != QStringLiteral("file"))
		return;
	const QString sourcePath = AUrl.toLocalFile();
	if (sourcePath.isEmpty())
		return;
	const QString destination = QFileDialog::getSaveFileName(window->instance(),
		tr("Save file"), QFileInfo(sourcePath).fileName());
	if (!destination.isEmpty())
		QThreadPool::globalInstance()->start(QRunnable::create([sourcePath, destination]() {
			if (!QFile::copy(sourcePath, destination))
				qWarning() << "Failed to save Matrix file attachment";
		}));
}

void ChatMessageHandler::onMessageReady()
{
	IChatWindow *window = qobject_cast<IChatWindow *>(sender());
	if (!window) return;
	if (!window->conversationId().isEmpty())
	{
		const QString body = window->editWidget()->document()->toPlainText();
		IProtocolMessaging *selectedMessaging = nullptr;
		for (IProtocolMessaging *messaging : FProtocolMessaging)
			if (messaging && ProtocolMessageRouting::hasExactStream(
				messaging->streamId(), window->accountId())) {
				selectedMessaging = messaging;
				break;
			}
		if (selectedMessaging) {
			const QString formattedBody = selectedMessaging->formatEmoticonsForSending(body);
			BasicMessage message(QString(), window->conversationId(), QString(), QString(),
				formattedBody, QDateTime::currentDateTimeUtc(),
				selectedMessaging->protocol(), BasicMessage::Outgoing);
			QTextEdit *editor = window->editWidget()->textEdit();
			const bool isMatrix = selectedMessaging->protocol() == QStringLiteral("matrix");
			const QString replyTargetId = isMatrix && editor
				? editor->property("vacuum.matrix.reply_event_id").toString() : QString();
			if (isMatrix) {
				QVariantMap metadata;
				metadata.insert(QStringLiteral("msgtype"), QStringLiteral("m.text"));
				metadata.insert(QStringLiteral("body"), body);
				if (MatrixReply::isEventId(replyTargetId))
					metadata.insert(QStringLiteral("reply_to_event_id"), replyTargetId);
				message.setMetadata(metadata);
			}
			if (selectedMessaging->sendMessage(message)) {
				if (!replyTargetId.isEmpty() && editor) {
					editor->setProperty("vacuum.matrix.reply_event_id", QVariant());
					editor->setPlaceholderText(
						editor->property("vacuum.matrix.reply_original_placeholder").toString());
					editor->setProperty("vacuum.matrix.reply_original_placeholder", QVariant());
				}
				window->editWidget()->clearEditor();
				return;
			}
		}
		qWarning() << "Protocol text message not routed: conversation=" << window->conversationId()
			<< "windowAccountSet=" << !window->accountId().isEmpty();
		return;
	}
	if (FMessageProcessor)
	{
		Message message;
		message.setTo(window->contactJid().full()).setType(Message::Chat);
		FMessageProcessor->textToMessage(message,window->editWidget()->document());
		if (!message.body().isEmpty() && FMessageProcessor->sendMessage(window->streamJid(),message,IMessageProcessor::MessageOut))
			window->editWidget()->clearEditor();
	}
}

void ChatMessageHandler::onWindowActivated()
{
	IChatWindow *window = qobject_cast<IChatWindow *>(sender());
	if (window)
	{
		removeNotifiedMessages(window);
		for (IProtocolMessaging *messaging : FProtocolMessaging)
		{
			if (!messaging)
				continue;
			if (messaging->streamId() != window->accountId())
			{
				messaging->setActiveConversation(QString());
				continue;
			}
			const QString conversationId = window->conversationId();
			messaging->setActiveConversation(conversationId);
			const QString eventId = messaging->latestConversationEventId(conversationId);
			if (!eventId.isEmpty())
				messaging->markConversationRead(conversationId, eventId);
		}
		if (FDestroyTimers.contains(window))
			delete FDestroyTimers.take(window);
	}
}

void ChatMessageHandler::onWindowClosed()
{
	IChatWindow *window = qobject_cast<IChatWindow *>(sender());
	if (window)
	{
		IChatWindow *activeWindow = NULL;
		if (FMessageWidgets)
			foreach (IChatWindow *candidate, FMessageWidgets->chatWindows())
				if (candidate != window && candidate->isActiveTabPage())
				{
					activeWindow = candidate;
					break;
				}
		for (IProtocolMessaging *messaging : FProtocolMessaging)
			if (messaging)
				messaging->setActiveConversation(activeWindow && activeWindow->accountId() == messaging->streamId()
					? activeWindow->conversationId() : QString());
		int destroyTimeout = Options::node(OPV_MESSAGES_CLEANCHATTIMEOUT).value().toInt();
		if (destroyTimeout>0 && !FNotifiedMessages.contains(window))
		{
			if (!FDestroyTimers.contains(window))
			{
				QTimer *timer = new QTimer;
				timer->setSingleShot(true);
				connect(timer,SIGNAL(timeout()),window->instance(),SLOT(deleteLater()));
				FDestroyTimers.insert(window,timer);
			}
			FDestroyTimers[window]->start(destroyTimeout*60*1000);
		}
	}
}

void ChatMessageHandler::onWindowDestroyed()
{
	IChatWindow *window = qobject_cast<IChatWindow *>(sender());
	if (FWindows.contains(window))
	{
		removeNotifiedMessages(window);
		if (FDestroyTimers.contains(window))
			delete FDestroyTimers.take(window);
		FConversationHistoryLoaded.remove(window->streamJid().bare() + QChar('\n') + window->contactJid().bare());
		FWindows.removeAt(FWindows.indexOf(window));
		FWindowStatus.remove(window);
		FPendingMessages.remove(window);
		FHistoryRequests.remove(FHistoryRequests.key(window));
	}
}

void ChatMessageHandler::onWindowNotifierActiveNotifyChanged(int ANotifyId)
{
	Q_UNUSED(ANotifyId);
	ITabPageNotifier *notifier = qobject_cast<ITabPageNotifier *>(sender());
	IChatWindow *window = notifier!=NULL ? qobject_cast<IChatWindow *>(notifier->tabPage()->instance()) : NULL;
	if (window)
		updateWindow(window);
}

void ChatMessageHandler::onWindowInfoFieldChanged(int AField, const QVariant &AValue)
{
	Q_UNUSED(AValue);
	if (AField==IInfoWidget::ContactShow || AField==IInfoWidget::ContactStatus ||
		AField==IInfoWidget::ContactName || AField==IInfoWidget::ContactAvatar)
	{
		IInfoWidget *widget = qobject_cast<IInfoWidget *>(sender());
		IChatWindow *window = widget!=NULL ? findWindow(widget->streamJid(),widget->contactJid()) : NULL;
		if (window)
		{
			if (AField==IInfoWidget::ContactShow || AField==IInfoWidget::ContactStatus)
			{
				QString status = widget->field(IInfoWidget::ContactStatus).toString();
				QString show = FStatusChanger ? FStatusChanger->nameByShow(widget->field(IInfoWidget::ContactShow).toInt()) : QString();
				WindowStatus &wstatus = FWindowStatus[window];
				if (Options::node(OPV_MESSAGES_SHOWSTATUS).value().toBool() && wstatus.lastStatusShow!=status+show)
				{
					QString message = tr("%1 changed status to [%2] %3").arg(widget->field(IInfoWidget::ContactName).toString()).arg(show).arg(status);
					showStyledStatus(window,message);
				}
				wstatus.lastStatusShow = status+show;
			}
			updateWindow(window);
		}
	}
}

void ChatMessageHandler::onStatusIconsChanged()
{
	foreach(IChatWindow *window, FWindows)
		updateWindow(window);
}

void ChatMessageHandler::onShowWindowAction(bool)
{
	Action *action = qobject_cast<Action *>(sender());
	if (action)
	{
		Jid streamJid = action->data(ADR_STREAM_JID).toString();
		Jid contactJid = action->data(ADR_CONTACT_JID).toString();
		messageShowWindow(MHO_CHATMESSAGEHANDLER,streamJid,contactJid,Message::Chat,IMessageHandler::SM_SHOW);
	}
}

void ChatMessageHandler::onClearWindowAction(bool)
{
	Action *action = qobject_cast<Action *>(sender());
	IChatWindow *window = action!=NULL ? qobject_cast<IChatWindow *>(action->parent()) : NULL;
	if (window)
	{
		IMessageStyle *style = window->viewWidget()!=NULL ? window->viewWidget()->messageStyle() : NULL;
		if (style!=NULL)
		{
			IMessageStyleOptions soptions = FMessageStyles->styleOptions(Message::Chat);
			style->changeOptions(window->viewWidget()->styleWidget(),soptions,true);
		}
	}
}

void ChatMessageHandler::onShortcutActivated(const QString &AId, QWidget *AWidget)
{
	if (FRostersView && AWidget==FRostersView->instance() && !FRostersView->hasMultiSelection())
	{
		QList<IRosterIndex *> indexes = FRostersView->selectedRosterIndexes();
		if (AId==SCT_ROSTERVIEW_SHOWCHATDIALOG && isSelectionAccepted(indexes))
		{
			IRosterIndex *index = indexes.first();
			messageShowWindow(MHO_CHATMESSAGEHANDLER,index->data(RDR_STREAM_JID).toString(),index->data(RDR_FULL_JID).toString(),Message::Chat,IMessageHandler::SM_SHOW);
		}
	}
}

void ChatMessageHandler::onArchiveMessagesLoaded(const QString &AId, const IArchiveCollectionBody &ABody)
{
	if (FHistoryRequests.contains(AId))
	{
		IChatWindow *window = FHistoryRequests.take(AId);
		setMessageStyle(window);

		int messageItEnd = 0;
		QList<Message> pendingMessages = FPendingMessages.take(window);
		while (messageItEnd<pendingMessages.count() && messageItEnd<ABody.messages.count())
		{
			const Message &hmessage = ABody.messages.at(messageItEnd);
			const Message &pmessage = pendingMessages.at(pendingMessages.count()-messageItEnd-1);
			if (hmessage.body()==pmessage.body() && qAbs(hmessage.dateTime().secsTo(pmessage.dateTime()))<HISTORY_DUBLICATE_DELTA)
				messageItEnd++;
			else
				break;
		}

		int messageIt = ABody.messages.count()-1;
		QMultiMap<QDateTime,QString>::const_iterator noteIt = ABody.notes.constBegin();
		while (messageIt>=messageItEnd || noteIt!=ABody.notes.constEnd())
		{
			if (messageIt>=messageItEnd && (noteIt==ABody.notes.constEnd() || ABody.messages.at(messageIt).dateTime()<noteIt.key()))
			{
				showStyledMessage(window,ABody.messages.at(messageIt));
				messageIt--;
			}
			else if (noteIt != ABody.notes.constEnd())
			{
				showStyledStatus(window,noteIt.value(),true,noteIt.key());
				++noteIt;
			}
		}

		foreach(Message message, pendingMessages)
			showStyledMessage(window,message);

		WindowStatus &wstatus = FWindowStatus[window];
		wstatus.startTime = !ABody.messages.isEmpty() ? ABody.messages.last().dateTime() : QDateTime();
	}
}

void ChatMessageHandler::onArchiveRequestFailed(const QString &AId, const XmppError &AError)
{
	if (FHistoryRequests.contains(AId))
	{
		IChatWindow *window = FHistoryRequests.take(AId);
		showStyledStatus(window,tr("Failed to load history: %1").arg(AError.errorMessage()),true);
		FPendingMessages.remove(window);
	}
}

void ChatMessageHandler::onRosterIndexContextMenu(const QList<IRosterIndex *> &AIndexes, quint32 ALabelId, Menu *AMenu)
{
	if (ALabelId==AdvancedDelegateItem::DisplayId && isSelectionAccepted(AIndexes))
	{
		if (!FRostersView->hasMultiSelection())
		{
			Action *action = new Action(AMenu);
			action->setText(tr("Open chat dialog"));
			action->setIcon(RSR_STORAGE_MENUICONS,MNI_CHAT_MHANDLER_MESSAGE);
			action->setData(ADR_STREAM_JID,AIndexes.first()->data(RDR_STREAM_JID));
			action->setData(ADR_CONTACT_JID,AIndexes.first()->data(RDR_FULL_JID));
			action->setShortcutId(SCT_ROSTERVIEW_SHOWCHATDIALOG);
			AMenu->addAction(action,AG_RVCM_CHATMESSAGEHANDLER,true);
			connect(action,SIGNAL(triggered(bool)),SLOT(onShowWindowAction(bool)));

			IRosterIndex *index = AIndexes.first();
			if (index->type()==RIT_CONTACT)
			{
				const QString conversationId = index->data(RDR_CONVERSATION_ID).toString();
				const QString accountId = index->data(RDR_ACCOUNT_ID).toString();
				const QString streamId = conversationId.isEmpty() || accountId.isEmpty()
					? Jid(index->data(RDR_STREAM_JID).toString()).pBare() : accountId;
				const QString targetId = conversationId.isEmpty()
					? Jid(index->data(RDR_PREP_BARE_JID).toString()).pBare() : conversationId;
				if (!streamId.isEmpty() && !targetId.isEmpty())
				{
					Action *mute = new Action(AMenu);
					mute->setText(tr("Mute notifications"));
					mute->setCheckable(true);
					mute->setChecked(messageNotificationMuted(streamId,targetId));
					connect(mute,&QAction::toggled,this,[streamId,targetId](bool AMuted) {
						setMessageNotificationMuted(streamId,targetId,AMuted);
					});
					AMenu->addAction(mute,AG_RVCM_CHATMESSAGEHANDLER,true);
				}
			}
		}
	}
}

void ChatMessageHandler::onPresenceItemReceived(IPresence *APresence, const IPresenceItem &AItem, const IPresenceItem &ABefore)
{
	if (!AItem.itemJid.resource().isEmpty() && AItem.show!=IPresence::Offline && AItem.show!=IPresence::Error && (ABefore.show==IPresence::Offline || ABefore.show==IPresence::Error))
	{
		IChatWindow *window = findSubstituteWindow(APresence->streamJid(),AItem.itemJid);
		if (window && window->contactJid()!=AItem.itemJid)
			window->setContactJid(AItem.itemJid);
	}
}

void ChatMessageHandler::onStyleOptionsChanged(const IMessageStyleOptions &AOptions, int AMessageType, const QString &AContext)
{
	if (AMessageType==Message::Chat && AContext.isEmpty())
	{
		foreach (IChatWindow *window, FWindows)
		{
			IMessageStyle *style = window->viewWidget()!=NULL ? window->viewWidget()->messageStyle() : NULL;
			if (style==NULL || !style->changeOptions(window->viewWidget()->styleWidget(),AOptions,false))
			{
				setMessageStyle(window);
				showHistory(window);
			}
		}
	}
}

bool ChatMessageHandler::eventFilter(QObject *AWatched, QEvent *AEvent)
{
	if (AEvent->type() == QEvent::KeyPress) {
		QKeyEvent *keyEvent = static_cast<QKeyEvent *>(AEvent);
		if (keyEvent->key() == Qt::Key_Escape) {
			QTextEdit *editor = qobject_cast<QTextEdit *>(AWatched);
			if (editor) {
				QString replyEventId = editor->property("vacuum.matrix.reply_event_id").toString();
				if (!replyEventId.isEmpty()) {
					// Cancel the active reply
					editor->setProperty("vacuum.matrix.reply_event_id", QVariant());
					QString originalPlaceholder = editor->property("vacuum.matrix.reply_original_placeholder").toString();
					if (!originalPlaceholder.isEmpty()) {
						editor->setPlaceholderText(originalPlaceholder);
						editor->setProperty("vacuum.matrix.reply_original_placeholder", QVariant());
					}
					return true; // Consume the event so it doesn't trigger window close
				}
			}
		}
	}
	return QObject::eventFilter(AWatched, AEvent);
}

void ChatMessageHandler::onReplyEscFilterDestroyed()
{
	FReplyEscFilter = NULL;
}

void ChatMessageHandler::showRoomInviteDialog(QWidget *AParent, IProtocolMessaging *AMessaging,
	const ProtocolRoom &ARoom)
{
	if (!AMessaging || !AMessaging->supportsRoomInvites() || ARoom.id.isEmpty())
		return;

	QDialog *dialog = new QDialog(AParent);
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->setWindowTitle(tr("Invite People to %1").arg(ARoom.name.isEmpty() ? ARoom.id : ARoom.name));
	QVBoxLayout *layout = new QVBoxLayout(dialog);
	layout->setContentsMargins(12, 12, 12, 12);
	layout->setSpacing(8);

	QLabel *infoLabel = new QLabel(tr("Enter a Matrix user ID to invite (e.g. @user:server):"), dialog);
	infoLabel->setWordWrap(true);
	layout->addWidget(infoLabel);

	QLineEdit *userIdEdit = new QLineEdit(dialog);
	userIdEdit->setPlaceholderText(tr("@user:server"));
	layout->addWidget(userIdEdit);

	QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
	connect(buttons, &QDialogButtonBox::accepted, dialog,
		[this, AMessaging, ARoom, userIdEdit, dialog]() {
			const QString userId = userIdEdit->text().trimmed();
			if (!AMessaging->inviteUserToRoom(ARoom.id, userId)) {
				QMessageBox::warning(dialog, tr("Invite failed"),
					tr("The Matrix invitation could not be queued. Check the user ID and connection."));
				return;
			}
			dialog->accept();
		});
	connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
	layout->addWidget(buttons);

	dialog->resize(360, 300);
	dialog->exec();
}
