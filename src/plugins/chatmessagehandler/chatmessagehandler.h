#ifndef CHATMESSAGEHANDLER_H
#define CHATMESSAGEHANDLER_H

#include "protocolmessagehistory.h"
#include "protocolmessagestatus.h"

#define CHATMESSAGEHANDLER_UUID QUuid("{b60cc0e4-8006-4909-b926-fcb3cbc506f0}")

#include <QTimer>
#include <QSet>
#include <QPointer>
#include <functional>
#include "protocolmessagehistory.h"
#include <definitions/messagehandlerorders.h>
#include <definitions/rosterindextyperole.h>
#include <definitions/rosterclickhookerorders.h>
#include <definitions/rosternotifyorders.h>
#include <definitions/recentitemtypes.h>
#include <definitions/notificationtypes.h>
#include <definitions/notificationdataroles.h>
#include <definitions/notificationtypeorders.h>
#include <definitions/tabpagenotifypriorities.h>
#include <definitions/messagedataroles.h>
#include <definitions/vcardvaluenames.h>
#include <definitions/actiongroups.h>
#include <definitions/resources.h>
#include <definitions/menuicons.h>
#include <definitions/soundfiles.h>
#include <definitions/shortcuts.h>
#include <definitions/optionnodes.h>
#include <definitions/optionvalues.h>
#include <definitions/optionwidgetorders.h>
#include <definitions/toolbargroups.h>
#include <definitions/xmppurihandlerorders.h>
#include <interfaces/ipluginmanager.h>
#include <interfaces/imessageprocessor.h>
#include <interfaces/iprotocolmessaging.h>
#include <interfaces/iprotocolroster.h>
#include <interfaces/imessagewidgets.h>
#include <interfaces/imessagestyles.h>
#include <interfaces/imessagearchiver.h>
#include <interfaces/inotifications.h>
#include <interfaces/ioptionsmanager.h>
#include <interfaces/istatusicons.h>
#include <interfaces/irostersview.h>
#include <interfaces/irostersmodel.h>
#include <interfaces/ipresence.h>
#include <interfaces/ivcard.h>
#include <interfaces/iavatars.h>
#include <interfaces/istatuschanger.h>
#include <interfaces/ixmppuriqueries.h>
#include <interfaces/irecentcontacts.h>
#include <interfaces/ifiletransfer.h>
#include <utils/widgetmanager.h>
#include <utils/options.h>
#include <utils/shortcuts.h>
#include <utils/textmanager.h>
#include "usercontextmenu.h"

struct WindowStatus
{
	QDateTime startTime;
	QDateTime createTime;
	QString lastStatusShow;
	QDate lastDateSeparator;
};

struct ProtocolReactionSource
{
	QString targetKey;
	QString reactionKey;
	QString senderId;
};

class IProtocolCapabilities;

class ChatMessageHandler :
	public QObject,
	public IPlugin,
	public IMessageHandler,
	public IXmppUriHandler,
	public IRostersClickHooker,
	public IOptionsHolder
{
	Q_OBJECT;
	Q_PLUGIN_METADATA(IID "org.vacuum-im.chatmessagehandler")
	Q_INTERFACES(IPlugin IMessageHandler IRostersClickHooker IXmppUriHandler IOptionsHolder);
public:
	ChatMessageHandler();
	~ChatMessageHandler();
	//IPlugin
	virtual QObject *instance() { return this; }
	virtual QUuid pluginUuid() const { return CHATMESSAGEHANDLER_UUID; }
	virtual void pluginInfo(IPluginInfo *APluginInfo);
	virtual bool initConnections(IPluginManager *APluginManager, int &AInitOrder);
	virtual bool initObjects();
	virtual bool initSettings();
	virtual bool startPlugin();
	//IXmppUriHandler
	virtual bool xmppUriOpen(const Jid &AStreamJid, const Jid &AContactJid, const QString &AAction, const QMultiMap<QString, QString> &AParams);
	//IRostersClickHooker
	virtual bool rosterIndexSingleClicked(int AOrder, IRosterIndex *AIndex, const QMouseEvent *AEvent);
	virtual bool rosterIndexDoubleClicked(int AOrder, IRosterIndex *AIndex, const QMouseEvent *AEvent);
	//IMessageHandler
	virtual bool messageCheck(int AOrder, const Message &AMessage, int ADirection);
	virtual bool messageDisplay(const Message &AMessage, int ADirection);
	virtual INotification messageNotify(INotifications *ANotifications, const Message &AMessage, int ADirection);
	virtual bool messageShowWindow(int AMessageId);
	virtual bool messageShowWindow(int AOrder, const Jid &AStreamJid, const Jid &AContactJid, Message::MessageType AType, int AShowMode);
	//IOptionsHolder
	virtual QMultiMap<int, IOptionsWidget *> optionsWidgets(const QString &ANodeId, QWidget *AParent);
protected:
	IChatWindow *getWindow(const Jid &AStreamJid, const Jid &AContactJid);
	IChatWindow *findWindow(const Jid &AStreamJid, const Jid &AContactJid) const;
	IChatWindow *findSubstituteWindow(const Jid &AStreamJid, const Jid &AContactJid) const;
	void updateWindow(IChatWindow *AWindow);
	void setupProtocolWindow(IChatWindow *AWindow, IProtocolMessaging *AMessaging);
	void setupFileTransferAction(IChatWindow *AWindow, IProtocolMessaging *AMessaging = nullptr);
	void setupRoomSidebar(IChatWindow *AWindow, IProtocolMessaging *AMessaging);
	void showRoomMemberProfile(QWidget *AParent, IProtocolMessaging *AMessaging,
		const ProtocolRoom &ARoom, const ProtocolRosterEntry &AMember);
	void showRoomInviteDialog(QWidget *AParent, IProtocolMessaging *AMessaging,
		const ProtocolRoom &ARoom);
	void renderProtocolMessage(IChatWindow *AWindow, IProtocolMessaging *AMessaging, const BasicMessage &AMessage);
	void renderProtocolHistory(IChatWindow *AWindow, IProtocolMessaging *AMessaging);
	void rebuildProtocolConversation(IChatWindow *AWindow, IProtocolMessaging *AMessaging,
		const QString &AHistoryKey);
	void scheduleProtocolConversationRebuild(IChatWindow *AWindow, IProtocolMessaging *AMessaging,
		const QString &AHistoryKey, QObject *AProtocolObject,
		std::function<void()> AAfterRebuild = std::function<void()>(),
		std::function<void()> ABeforeRebuild = std::function<void()>());
	void bindProtocolHistoryPaging(IChatWindow *AWindow, IProtocolMessaging *AMessaging,
		const QString &AHistoryKey, QObject *AProtocolObject);
	void queueOlderProtocolHistoryPage(QObject *AScrollBarObject);
	void requestOlderProtocolHistoryPage(const QString &AHistoryKey, QObject *AProtocolObject);
	void sortProtocolMessagesChronologically(QList<BasicMessage> &AMessages) const;
	void addProtocolReaction(IChatWindow *AWindow, IProtocolMessaging *AMessaging,
		const BasicMessage &AMessage);
	void removeProtocolReaction(IChatWindow *AWindow, const QString &eventPrefix,
		const QString &eventId);
	void updateProtocolReactionDecoration(IChatWindow *AWindow, const QString &targetKey,
		const QString &targetEventId);
	void updateProtocolMessageStatusDecoration(IChatWindow *AWindow,
		IProtocolMessaging *AMessaging, const BasicMessage &AMessage);
	void removeNotifiedMessages(IChatWindow *AWindow);
	void showHistory(IChatWindow *AWindow);
	void setMessageStyle(IChatWindow *AWindow);
	void fillContentOptions(IChatWindow *AWindow, IMessageContentOptions &AOptions) const;
	void showDateSeparator(IChatWindow *AWindow, const QDateTime &ADateTime);
	void showStyledStatus(IChatWindow *AWindow, const QString &AMessage, bool ADontSave=false, const QDateTime &ATime=QDateTime::currentDateTime());
	void showStyledMessage(IChatWindow *AWindow, const Message &AMessage);
	bool isSelectionAccepted(const QList<IRosterIndex *> &ASelected) const;
protected:
	virtual bool eventFilter(QObject *AWatched, QEvent *AEvent) override;
protected slots:
	void onReplyEscFilterDestroyed();
	void onProtocolChatWindowCreated(IChatWindow *AWindow);
	void onProtocolMessageReceived(const BasicMessage &AMessage);
	void onProtocolHistoryLoaded(const QString &ARoomId);
	void onProtocolViewContextMenu(const QPoint &APosition,
		const QTextDocumentFragment &ASelection, Menu *AMenu);
	void onProtocolRosterChanged();
	void onProtocolAvatarUpdated(const QString &key);
	void onProtocolMessageDeliveryChanged(const QString &conversationId,
		const QString &transactionId, const QString &status, const QString &serverEventId);
	void onProtocolMessageReadReceiptReceived(const QString &conversationId,
		const QString &eventId, const QString &readerId, const QString &readerDisplayName);
	void onProtocolUrlClicked(const QUrl &AUrl);
	void onMessageReady();
	void onWindowActivated();
	void onWindowClosed();
	void onWindowDestroyed();
	void onWindowNotifierActiveNotifyChanged(int ANotifyId);
	void onWindowInfoFieldChanged(int AField, const QVariant &AValue);
	void onStatusIconsChanged();
	void onShowWindowAction(bool);
	void onClearWindowAction(bool);
	void onShortcutActivated(const QString &AId, QWidget *AWidget);
	void onArchiveMessagesLoaded(const QString &AId, const IArchiveCollectionBody &ABody);
	void onArchiveRequestFailed(const QString &AId, const XmppError &AError);
	void onRosterIndexContextMenu(const QList<IRosterIndex *> &AIndexes, quint32 ALabelId, Menu *AMenu);
	void onPresenceItemReceived(IPresence *APresence, const IPresenceItem &AItem, const IPresenceItem &ABefore);
	void onStyleOptionsChanged(const IMessageStyleOptions &AOptions, int AMessageType, const QString &AContext);
private:
	IProtocolMessaging *findLocalHistoryProvider(const QString &ARosterStreamId,
		IProtocolRoster *&AProviderRoster, IProtocolCapabilities *&ACapabilities) const;
	IProtocolMessaging *findLocalHistoryProviderForAccount(const AccountId &AAccountId,
		IProtocolRoster *&AProviderRoster, IProtocolCapabilities *&ACapabilities) const;
	void setupLocalHistoryAction(IChatWindow *AWindow);
	void showLocalConversationHistory(const QString &ARosterStreamId,
		const ConversationId &AConversationId);
	IMessageWidgets *FMessageWidgets;
	IMessageProcessor *FMessageProcessor;
	IPluginManager *FPluginManager;
	QList<IProtocolMessaging *> FProtocolMessaging;
	ProtocolMessageStatus::StateStore FProtocolMessageStatus;
	IProtocolRoster *FProtocolRoster;
	IMessageStyles *FMessageStyles;
	IPresencePlugin *FPresencePlugin;
	IMessageArchiver *FMessageArchiver;
	IRostersView *FRostersView;
	IRostersModel *FRostersModel;
	IStatusIcons *FStatusIcons;
	IStatusChanger *FStatusChanger;
	IXmppUriQueries *FXmppUriQueries;
	IOptionsManager *FOptionsManager;
	IRecentContacts *FRecentContacts;
	IFileTransfer *FFileTransfer;
private:
	QList<IChatWindow *> FWindows;
	QMap<IChatWindow *, QTimer *> FDestroyTimers;
	QMultiMap<IChatWindow *, int> FNotifiedMessages;
	QMap<IChatWindow *, WindowStatus> FWindowStatus;
	QSet<QString> FConversationHistoryLoaded;
	QSet<QString> FProtocolRenderedMessages;
	QMap<QString, ProtocolReactionSource> FProtocolReactionEvents;
	QMap<QString, QMap<QString, QMap<QString, QSet<QString>>>> FProtocolReactionSenders;
	QSet<QString> FProtocolReadyAvatarKeys;
	QMap<QString, QString> FProtocolEventMessageIds;
	QMap<QString, bool> FProtocolMessageDirections;
	QMap<QString, QString> FProtocolMessageRelationTypes;
	QSet<QString> FProtocolRedactedMessages;
	QSet<QString> FProtocolHistoryLoading;
	QSet<QString> FProtocolHistoryLoaded;
	QMap<QString, QList<BasicMessage>> FPendingProtocolHistoryMessages;
	QMap<QString, QList<BasicMessage>> FProtocolConversationMessages;
	QMap<QString, QPointer<QObject>> FProtocolHistoryScrollBars;
	QMap<QString, QPointer<QObject>> FProtocolHistoryProviders;
	QMap<QString, ProtocolMessageHistory::OlderHistoryPageState> FProtocolHistoryPageStates;
	QSet<QObject *> FProtocolHistorySignalBars;
	QMap<QString, QList<std::function<void()>>> FProtocolRebuildBeforeCallbacks;
	QMap<QString, QList<std::function<void()>>> FProtocolRebuildAfterCallbacks;
	ProtocolMessageHistory::ConversationRebuildScheduler FProtocolRebuildScheduler;
private:
	QMap<QString, IChatWindow *> FHistoryRequests;
	QMap<IChatWindow *, QList<Message> > FPendingMessages;
	QObject *FReplyEscFilter;
	QString FRoomInviteTarget;
};

#endif // CHATMESSAGEHANDLER_H
