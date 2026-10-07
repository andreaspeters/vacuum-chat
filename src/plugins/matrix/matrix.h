#ifndef MATRIX_H
#define MATRIX_H

#include <QObject>
#include <QMap>
#include <QSet>
#include <QPointer>
#include <QUuid>
#include <QUrl>
#include <interfaces/ipluginmanager.h>
#include <interfaces/iaccountmanager.h>
#include <interfaces/ioptionsmanager.h>
#include <interfaces/iprotocolpresence.h>
#include <interfaces/iprotocolroster.h>
#include <interfaces/iprotocolmessaging.h>
#include <interfaces/iprotocolnotifications.h>
#include <interfaces/iprotocolcapabilities.h>
#include <interfaces/iprotocolcontactactions.h>
#include <interfaces/iprotocolaccountavataractions.h>
#include <interfaces/iprotocolprofileactions.h>
#include <interfaces/iavatars.h>
#include <interfaces/iemoticons.h>
#include <interfaces/irostersview.h>
#include "matrixnetwork.h"
#include "matrixdatabaseworker.h"
#include "matrixverificationdialog.h"
#include "matrixssssdialog.h"
#include <QtPlugin>
#include <QDialog>
#include <QThread>

class IMessageWidgets;

class Matrix : public QObject, public IPlugin, public IProtocolPresence, public IProtocolRoster, public IProtocolMessaging, public IProtocolNotifications, public IProtocolCapabilities, public IProtocolContactActions, public IProtocolAccountAvatarActions, public IProtocolProfileActions
{
	Q_OBJECT
	Q_PLUGIN_METADATA(IID "Vacuum.Core.IPlugin/1.0" FILE "matrix.json")
	Q_INTERFACES(IPlugin IProtocolPresence IProtocolRoster IProtocolMessaging IProtocolNotifications IProtocolCapabilities IProtocolContactActions IProtocolAccountAvatarActions IProtocolProfileActions)

public:
	Matrix();
	virtual ~Matrix();

	virtual QObject *instance() { return this; }
	virtual QUuid pluginUuid() const { return FUuid; }
	virtual void pluginInfo(IPluginInfo *APluginInfo);
	virtual bool initConnections(IPluginManager *APluginManager, int &AInitOrder);
	virtual bool initObjects();
	virtual bool initSettings();
	virtual bool startPlugin();
	virtual QString streamId() const;
	virtual bool setPresence(int AShow, const QString &AStatus);
	virtual QString accountId() const override;
	ProtocolAccountIdentifier accountIdentifier() const override;
	IProtocolCapabilities::Capabilities capabilitiesForAccount(const AccountId &accountId,
		const ConversationId &targetId = ConversationId()) const override;
	bool showAddContactDialog(const AccountId &accountId) override;
	bool setAccountAvatar(const AccountId &accountId, const QByteArray &imageData) override;
	bool showProfile(const AccountId &accountId, const UserId &userId) override;
	bool editProfile(const AccountId &accountId) override;
	virtual QString protocol() const { return QStringLiteral("matrix"); }
	virtual QString formatEmoticonForSending(const QString &iconKey) const;
	virtual QString formatEmoticonsForSending(const QString &text) const;
	virtual QString formatEmoticonsForDisplay(const QString &text) const;
	virtual QString emojiPickerType() const;
	virtual QList<ProtocolNotification> notifications() const override;
	virtual void appendNotification(const ProtocolNotification &notification) override;
	virtual void removeNotification(const QString &id) override;
	virtual bool sendMessage(const BasicMessage &message);
	virtual bool supportsRoomInvites() const override;
	virtual bool inviteUserToRoom(const ConversationId &roomId, const UserId &userId) override;
	virtual bool supportsReactions(const ConversationId &conversationId) const;
	virtual bool supportsReplies(const ConversationId &conversationId) const override
	{ return supportsReactions(conversationId); }
	virtual bool providesAvatarUpdateSignals() const override { return true; }
	virtual bool providesHistoryLoadedSignals() const override { return true; }
	virtual bool providesMessageDeliverySignals() const override { return true; }
	virtual bool providesReadReceiptSignals() const override { return true; }
	virtual bool supportsConversationMedia() const override { return true; }
	virtual bool supportsFileTransfer() const override { return true; }
	virtual bool sendReaction(const ConversationId &conversationId, const MessageId &eventId,
		const QString &key);
	virtual bool isEventIdLike(const QString &eventId) const override;
	virtual QString replyPreviewHtml(const QString &senderName, const QString &excerpt,
		const QString &avatarResourceUrl) const override;
	virtual QString stripReplyFallback(const QString &body, const QString &formatType) const override;
	virtual QString sanitizeHtml(const QString &html) const override;
	virtual QString highlightMentions(const QString &text) const override;
	virtual bool supportsTyping(const ConversationId &conversationId) const;
	virtual void setTyping(const ConversationId &conversationId, ProtocolTypingStatus status);
	virtual QList<BasicMessage> conversationHistory(const QString &conversationId) const;
	bool supportsOlderHistory(const ConversationId &conversationId) const override;
	bool requestOlderHistoryPage(const ConversationId &conversationId,
		const BasicMessage &beforeMessage, int limit, QObject *callbackContext,
		ProtocolHistoryPageCallback callback) override;
	virtual void setActiveConversation(const QString &conversationId) const override;
	virtual void loadConversationAvatars(const QString &conversationId) const override;
	virtual void loadUserAvatar(const QString &conversationId, const QString &userId) const override;
	virtual void loadConversationMedia(const BasicMessage &message) const override;
	virtual QString conversationDisplayName(const QString &conversationId) const;
	virtual QString conversationAvatarPath(const QString &conversationId) const;
	virtual QString userAvatarPath(const QString &conversationId, const QString &userId) const;
	virtual QString userAvatarKey(const QString &conversationId, const QString &userId) const override;
	virtual bool markConversationRead(const QString &conversationId, const QString &eventId);
	virtual QString latestConversationEventId(const QString &conversationId) const;
	virtual bool conversationIdForAddress(const Jid &address, QString &conversationId) const;
	virtual Jid addressForConversation(const QString &conversationId) const;
	virtual bool conversationOnline(const Jid &address) const;

	virtual QList<ProtocolRosterEntry> entries() const { return FProtocolEntries; }
	virtual QList<ProtocolRoom> rooms() const;
	virtual ProtocolRosterEntry entry(const QString &AId) const;
	virtual ProtocolRoom room(const QString &AId) const;
	virtual void loadRoomAvatar(const QString &roomId) const override;
	virtual int show() const { return FShow; }
	virtual QString status() const { return FStatus; }
	Q_INVOKABLE void requestCurrentSync(const QString &accountId);
	Q_INVOKABLE void requestSsssRecoveryForAccount(const QString &accountId);
	Q_INVOKABLE void requestRoomKeyImportForAccount(const QString &accountId);
	Q_INVOKABLE bool requestDeviceVerification(const QString &transactionId,
		const QString &userId, const QString &deviceId);
	Q_INVOKABLE void requestDeviceVerificationForAccount(const QString &accountId,
		const QString &userId, const QString &deviceId);
	Q_INVOKABLE bool cancelDeviceVerification(const QString &transactionId,
		const QString &userId, const QString &deviceId);
	Q_INVOKABLE bool acceptDeviceVerification(const QString &transactionId,
		const QString &userId, const QString &deviceId);
	Q_INVOKABLE bool startDeviceVerification(const QString &transactionId,
		const QString &userId, const QString &deviceId);
	Q_INVOKABLE bool confirmDeviceVerification(const QString &transactionId,
		const QString &userId, const QString &deviceId);

private slots:
	void onAccountShown(IAccount *AAccount);
	void onAccountAppended(IAccount *AAccount);
	void onAccountChanged(IAccount *AAccount, const OptionsNode &ANode);
	void onAccountHidden(IAccount *AAccount);

	void onLoginSuccess(const QString &AUserId, const QString &AAccessToken,
		const QString &ADeviceId, quint64 ASessionGeneration);
	void onLoginError(const QString &AError, quint64 ASessionGeneration);
	void onSyncError(const QString &AError);
	void onInitialSyncCompleted();
	void onSyncReceived(const QList<MatrixTextEvent> &events);
	void onMessageHistoryChanged(const QString &roomId, const QList<MatrixTextEvent> &events);
	void onRosterChanged(const QList<ProtocolRoom> &rooms);
	void onNetworkMessageReceived(const BasicMessage &message);
	void onAvatarImageReceived(const QString &key, const QImage &image);
	void onDisplayNameReceived(const QString &userId, const QString &displayName);
	void onRoomNameReceived(const QString &roomId, const QString &roomName);
	void onNetworkTypingChanged(const ProtocolTypingUpdate &update);
	void onNetworkReceiptReceived(const MatrixReceipt &receipt);
	void onNetworkNotificationEvent(const MatrixNotificationEvent &event);
	void onAccountAvatarUpdateFinished(const QString &userId, bool success,
		const QString &avatarUrl, const QString &error);
	void onAccountDisplayNameUpdateFinished(const QString &userId, bool success,
		const QString &displayName, const QString &error);


signals:
	void protocolPresenceChanged(const QString &AStreamId, int AShow, const QString &AStatus);
	void protocolPresenceClosed(const QString &AStreamId);
	void protocolRosterChanged();
	void protocolAvatarUpdated(const QString &key);
	void protocolMessageReceived(const BasicMessage &message);
	void protocolHistoryLoaded(const QString &roomId);
	void protocolTypingChanged(const ProtocolTypingUpdate &update);
	void protocolMessageDeliveryChanged(const QString &conversationId, const QString &transactionId,
		const QString &status, const QString &serverEventId);
	void protocolMessageReadReceiptReceived(const QString &conversationId, const QString &eventId,
		const QString &readerId, const QString &readerDisplayName);
	void protocolNotificationsChanged();
	void protocolVerificationStateChanged(const QString &transactionId, const QString &state);
	void protocolVerificationSasAvailable(const QString &transactionId,
		const QStringList &emoji, const QString &decimal);

private:
	friend class MatrixPresenceLifecycleTestAccess;

	QUuid FUuid;
	MatrixNetwork *FMatrixNetwork;
	QThread *FNetworkThread = nullptr;
	IAccountManager *FAccountManager;
	IOptionsManager *FOptionsManager;
	IRostersViewPlugin *FRostersViewPlugin = nullptr;
	IMessageWidgets *FMessageWidgets = nullptr;
	IAccount *FMatrixAccount;
	IAccount *FAccountManagerSlot;
	IAvatars *FAvatars = nullptr;
	IEmoticons *FEmoticons = nullptr;
	QList<ProtocolRosterEntry> FProtocolEntries;
	QList<ProtocolRoom> FProtocolRooms;
	QString FStreamId;
	int FShow;
	QString FStatus;
	mutable QString FActiveConversationId;
	QList<ProtocolNotification> FNotifications;
	QDialog *FInitialSyncWindow;
	QSet<QString> FNotificationEventIds;
	bool FNotificationsReady = false;

	void loadRoomsFromDatabase();
	void showRoomChatDialog(const QString &boundAccountId);
	void showVerificationDialog(const QString &transactionId, const QString &userId,
		const QString &deviceId);
	void onVerificationRequest(const QString &transactionId, const QString &userId,
		const QString &deviceId);
	void onVerificationSas(const QString &transactionId, const QStringList &emoji,
		const QString &decimal);
	void onVerificationState(const QString &transactionId, const QString &state);
	void onDeviceTrustChanged(const QString &userId, const QString &deviceId);
	void onSsssRecoveryFinished(bool success, const QString &error);
	void onCachedRoomsLoaded(const QList<MatrixCachedRoom> &rooms);
	void onCachedRoomsLoadFailed(const QString &error);
	void onCachedHistoryLoaded(const QString &roomId, const QList<MatrixTimelineEvent> &events);
	void onCachedHistoryLoadFailed(const QString &roomId, const QString &error);
	void onHistoryPageLoaded(const QString &roomId, const MatrixHistoryPageResult &result);
	void emitCachedHistoryBatch();
	void queueOrEmitHistoryMessage(const BasicMessage &message);
	void finishHistoryLoad(const QString &roomId);
	QList<BasicMessage> mergeHistoryMessagesChronologically(
		const QList<BasicMessage> &cached, const QList<BasicMessage> &pending) const;
	void loadNextAvatar();
	void enqueueAvatarLoad(const QString &key, const QString &url) const;
	void onCompleteCrossSigningKeysChecked(const QString &userId, bool complete);


	MatrixVerificationDialog *FVerificationDialog = nullptr;
	MatrixSsssDialog *FSsssDialog = nullptr;
	QThread *FDatabaseThread = nullptr;
	MatrixDatabaseWorker *FDatabaseWorker = nullptr;
	QString FDatabaseProfileDirectory;
	QString FDatabaseServerUrl;
	QString FDatabaseUserId;
	QString FNetworkUserId;
	QString FNetworkDeviceId;
	QByteArray FSsssMasterSecret;
	QByteArray FSsssSelfSigningSecret;
	QByteArray FSsssUserSigningSecret;
	bool FNetworkLoggedIn = false;
	bool FNetworkInitialSyncComplete = false;
	bool FLoginRequested = false;
	quint64 FLoginGeneration = 0;
	int FPendingShow = 0;
	QString FPendingStatus;
	QString FLoginAccountId;
	QString FLoginUserId;
	QString FDeferredVerificationTransactionId;
	QString FDeferredVerificationUserId;
	QString FDeferredVerificationDeviceId;
	QString FDeferredVerificationStateTransactionId;
	QString FDeferredVerificationState;
	QString FDeferredVerificationSasTransactionId;
	QStringList FDeferredVerificationSasEmoji;
	QString FDeferredVerificationSasDecimal;
	QMap<QString, QString> FLatestConversationEventIds;
	QList<BasicMessage> FCachedHistoryQueue;
	bool FCachedHistoryBatchScheduled = false;
	mutable QList<QPair<QString, QString>> FAvatarLoadQueue;
	mutable QSet<QString> FAvatarLoadSeen;
	mutable bool FAvatarLoadScheduled = false;

	mutable QSet<QString> FHistoryRequests;
	mutable QSet<QString> FHistoryLoadingRooms;
	QSet<QString> FHistoryQueuedRooms;
	mutable QMap<QString, QList<BasicMessage>> FPendingHistoryMessages;
	struct PendingHistoryPageRequest
	{
		QPointer<QObject> callbackContext;
		ProtocolHistoryPageCallback callback;
	};
	QMap<QString, PendingHistoryPageRequest> FPendingHistoryPageRequests;

};

#endif // MATRIX_H