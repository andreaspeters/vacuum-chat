#ifndef MATRIXNETWORK_H
#define MATRIXNETWORK_H

#include <QObject>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonObject>
#include <QJsonArray>
#include <QVector>
#include <QImage>
#include <QMap>
#include <QVariant>
#include <QtPlugin>
#include <interfaces/imessage.h>
#include <interfaces/iprotocolroster.h>
#include <interfaces/iprotocolnotifications.h>
#include <interfaces/iprotocolpresence.h>

struct MatrixTextEvent {
	QString eventId;
	QString roomId;
	QString userId;
	QString content;
	QString timestamp;
	QString eventType;
	QString messageType;
	QStringList attachments;
	QVariantMap metadata;

	BasicMessage toBasicMessage() const
	{
		return BasicMessage(eventId, roomId, userId, QString(), content,
			QDateTime::fromMSecsSinceEpoch(timestamp.toLongLong()),
			QStringLiteral("matrix"), BasicMessage::Incoming).setMetadata(metadata);
	}
};

/* Protocol-neutral type wrappers for Matrix-specific data */
struct MatrixPresenceEvent {
	QString userId;
    QString presence;      /* m.presence presence value */
    QString statusMessage; /* m.presence status message */
    qint64 lastActiveTs;    /* m.presence last_active_ts */
};

struct MatrixTypingEvent {
	QString roomId;
	QString userId;        /* m.room.typing user who started typing */
	bool active;           /* m.room.typing typing state true/false */
 };
struct MatrixReceipt {
	QString roomId;
	QString userId;
	QString eventId;
	QString receiptType;
	qint64 timestamp = 0;
};
Q_DECLARE_METATYPE(MatrixReceipt)

struct MatrixNotificationEvent {
	QString eventId;
	QString roomId;
	QString roomType;
	QString sender;
	QString type;
	QJsonObject content;
	qint64 timestamp = 0;
	bool historical = false;
};
Q_DECLARE_METATYPE(MatrixNotificationEvent)

#include "matrixdatabase.h"
#include <functional>
#include "matrixolm.h"
#include "matrixdirectroom.h"
#include "matrixpublicrooms.h"
class MatrixNetwork : public QObject
{
	Q_OBJECT
public:
	enum RequestType {
		RequestNone,
		RequestLogin,
		RequestSync,
		RequestSend
	};

	explicit MatrixNetwork(QObject *parent = nullptr);

	// Set server URL - validates and normalizes it
	Q_INVOKABLE void setServerUrl(const QString &serverUrl);
	Q_INVOKABLE void setDatabaseProfileDirectory(const QString &profileDirectory);

	// Getters for current state
	QString serverUrl() const { return FServerUrl; }
	QString normalizedServerUrl() const { return FNormalizedServerUrl; }
	bool isLoggedIn() const { return !FAccesToken.isEmpty(); }
	bool initialSyncComplete() const { return FInitialSyncComplete; }
	bool isLoginInProgress() const { return FInFlightRequest == RequestLogin; }
	QString deviceId() const { return FDeviceId; }
	QString userId() const { return FUserId; }

	Q_INVOKABLE QString login(const QString &userId, const QString &password, const QString &deviceId = QString());
	Q_INVOKABLE void loginWithAccessToken(const QString &userId, const QString &accessToken,
		const QString &deviceId = QString());

	Q_INVOKABLE void logout();  // Clears access token and sync state
	Q_INVOKABLE void shutdown(); // Abort network activity before thread teardown
	Q_INVOKABLE void sync(bool roomsOnly = false);    // Manual sync trigger
	Q_INVOKABLE void requestSsssRecovery(const QString &passphraseOrRecoveryKey);
	Q_INVOKABLE void requestSsssRecoveryFromVerifiedDevices();
	Q_INVOKABLE QByteArray takeSsssSecret(const QString &secretName);
	Q_INVOKABLE bool importRoomKeyFile(const QString &filePath, const QString &passphrase, QString &error);
	Q_INVOKABLE void uploadOwnDeviceSignature(const QByteArray &selfSigningSeed);
	Q_INVOKABLE void uploadRecoveredMasterKeySignature();
	Q_INVOKABLE void uploadUserMasterSignature(const QString &userId,
		const QByteArray &userSigningSeed);
	Q_INVOKABLE void setActiveRoom(const QString &roomId);
	Q_INVOKABLE void setDatabaseWorker(QObject *worker);
	Q_INVOKABLE void setPresence(const QString &userId, const QString &presence,
		const QString &statusMessage = QString());
	Q_INVOKABLE void setTyping(const QString &roomId, bool typing);
	Q_INVOKABLE void sendTextMessage(const QString &roomId, const QString &text, const QString &txnId);
	Q_INVOKABLE void sendMessageEvent(const QString &roomId, const QJsonObject &content, const QString &txnId = QString());
	Q_INVOKABLE void sendRoomEvent(const QString &roomId, const QString &eventType,
		const QJsonObject &content, const QString &txnId = QString());
	Q_INVOKABLE void joinRoom(const QString &roomId);
	Q_INVOKABLE void searchPublicRooms(const QString &directoryServer, const QString &searchTerm,
		int limit = 25, const QString &since = QString());
	Q_INVOKABLE void startDirectChat(const QString &userId);
	Q_INVOKABLE void leaveRoom(const QString &roomId);
	Q_INVOKABLE void uploadFileAndSend(const QString &roomId, const QString &filePath, const QString &mimeType,
		const QString &messageType, const QString &body, const QString &txnId);
	Q_INVOKABLE void retryFailedOutbox();
	Q_INVOKABLE void requestAvatar(const QString &key, const QString &mxcUrl);
	Q_INVOKABLE void requestImage(const MatrixTextEvent &event);
	void requestHistoricalImages(const QList<MatrixTimelineEvent> &events);
	Q_INVOKABLE void requestDisplayName(const QString &userId);
	Q_INVOKABLE void requestRoomName(const QString &roomId);
	Q_INVOKABLE void requestJoinedMembers(const QString &roomId);
	Q_INVOKABLE void refreshRosterSnapshot();
	Q_INVOKABLE void setKnownRoomTypes(const QVariantMap &roomTypes);
	Q_INVOKABLE bool requestSasVerification(const QString &transactionId, const QString &userId,
		const QString &deviceId);
	Q_INVOKABLE bool cancelSasVerification(const QString &transactionId, const QString &userId,
		const QString &deviceId);
	Q_INVOKABLE bool acceptSasVerification(const QString &transactionId, const QString &userId,
		const QString &deviceId);
	Q_INVOKABLE bool startSasVerification(const QString &transactionId, const QString &userId,
		const QString &deviceId);
	Q_INVOKABLE bool confirmSasVerification(const QString &transactionId, const QString &userId,
		const QString &deviceId);
	Q_INVOKABLE bool markRoomRead(const QString &roomId, const QString &eventId);
	QList<MatrixTextEvent> messageHistory(const QString &roomId);
	QList<MatrixTextEvent> historyBackfill(const QString &roomId, int limit);

signals:
	void connectionStateChanged(int state);  // 0=disconnected, 1=connecting, 2=connected
	void loginSuccess(const QString &userId, const QString &accessToken,
		const QString &deviceId);
	void loginError(const QString &error);
	void syncReceived(const QList<MatrixTextEvent> &events);
	void initialSyncCompleted();
	void ssssRecoveryFinished(bool success, const QString &error);
	void syncError(const QString &error);
	void messageSent(const QString &messageId);
	void sendError(const QString &error);
	void messageDeliveryChanged(const QString &conversationId, const QString &transactionId,
		const QString &status, const QString &serverEventId);
	void messageReceived(const BasicMessage &message);  // Protocol-neutral message signal
	void messageHistoryChanged(const QString &roomId, const QList<MatrixTextEvent> &events);
	void presenceReceived(const ProtocolPresenceUpdate &update);  // Protocol-neutral presence signal
	void typingChanged(const ProtocolTypingUpdate &update);  // Protocol-neutral typing signal
	void receiptReceived(const MatrixReceipt &receipt);
	void notificationEventReceived(const MatrixNotificationEvent &event);
	void avatarImageReceived(const QString &key, const QImage &image);
	void displayNameReceived(const QString &userId, const QString &displayName);
	void roomNameReceived(const QString &roomId, const QString &roomName);
	void publicRoomsReceived(const MatrixPublicRooms::Result &result);
	void directRoomCreated(const QString &userId, const QString &roomId, const QString &error);
	void rosterChanged(const QList<ProtocolRoom> &rooms);
	void roomKeyRequestReceived(const QString &sender, const QString &deviceId,
		const QString &roomId, const QString &sessionId, const QString &requestId);
	void deviceKeysReceived(const QString &userId, const QString &deviceId,
		const QJsonObject &deviceKeys);
	void oneTimeKeyReceived(const QString &userId, const QString &deviceId,
		const QString &keyId, const QString &key);
	void verificationStateChanged(const QString &transactionId, const QString &state);
	void verificationRequestReceived(const QString &transactionId, const QString &userId,
		const QString &deviceId);
	void deviceTrustChanged(const QString &userId, const QString &deviceId);
	void verificationSasAvailable(const QString &transactionId, const QStringList &emoji,
		const QString &decimal);

private slots:
	void onReplyFinished(QNetworkReply *reply);
	void onLoginFinished(QNetworkReply *reply);
	void uploadOlmKeys(int generateCount = 50, bool replaceFallback = false);
	void createSyncFilter(bool roomsOnly);
	void queryDeviceKeys(const QString &userId, const QString &deviceId = QString());
	void queryOwnDevices();
	void claimOneTimeKey(const QString &userId, const QString &deviceId);
	void onSyncFinished(QNetworkReply *reply);
	void onSendFinished(QNetworkReply *reply);
	void onReplyError(QNetworkReply *reply);

private:
	void runDatabase(const std::function<void(MatrixDatabase &)> &operation);
	bool validateAndNormalizeServerUrl(const QString &serverUrl, QString &normalized) const;
	QString constructUrl(const QString &path) const;
	QString generateTransactionId() const;

	// In-flight request tracking
	RequestType currentRequestType() const;
	bool isRequestInProgress() const;
	void setRequestType(RequestType type);

	void cleanupReply(QNetworkReply *reply);
	void scheduleSyncRetry(int delayMs = 1000);
	void loadMessageHistory();
	void saveMessageHistory() const;
	void restorePersistedRooms();
	void emitRosterSnapshot();
	bool mergeMessageEvent(const MatrixTextEvent &event);
	bool replacePendingEvent(const QString &roomId, const QString &transactionId,
		const MatrixTextEvent &serverEvent);
	void retryPendingEncryptedEvents(const QString &roomId, const QString &sessionId);
	void sendForwardedRoomKey(const QString &userId, const QString &deviceId,
		const QString &roomId, const QString &sessionId, const QString &requestId,
		quint32 minimumIndex = 0);
	void requestMissingRoomKey(const QString &sender, const QString &senderKey,
		const QString &roomId, const QString &sessionId,
		const QString &targetDeviceId = QString());
	bool sendEncryptedToDeviceEvent(const QString &userId, const QString &deviceId,
		const QString &eventType, const QJsonObject &eventContent,
		bool forceNewSession = false);
	void cancelMissingRoomKeyRequest(const QString &roomId, const QString &senderKey,
		const QString &sessionId);
	bool distributeOutboundRoomKey(const QString &roomId, const QString &sessionId,
		const QByteArray &sessionKey);
	bool sendVerificationEvent(const QString &eventType, const QString &transactionId,
		const QString &userId, const QString &deviceId, const QJsonObject &content);
	void uploadOwnMasterKeySignature(const QJsonObject &masterKey);
	void requestSsssKeyDescription(const QString &keyId);
	void requestSsssSecret(const QString &secretName);
	void requestSsssSecretFromDevices(const QString &secretName);
	bool validateRecoveredMasterSecret();
	bool isOwnDeviceCrossSigningVerified(const QString &deviceId) const;
	void changeRoomMembership(const QString &roomId, const QString &action);
	void queryDeviceKeysForRoom(const QString &roomId);

	QString FServerUrl;
	QString FNormalizedServerUrl;
	QString FDatabaseProfileDirectory;
	QString FAccesToken;
	QString FDeviceId;
	MatrixOlmCrypto FOlmCrypto;
	QString FUserId;
	QString FSyncToken;
	QString FActiveRoomId;
	QString FSyncFilterId;
	bool FFilterCreationAttempted = false;
	QMap<QString, ProtocolRoom> FRooms;
	QMap<QString, QString> FKnownRoomTypes;
	QSet<QString> FReplacedRoomIds;
	QSet<QString> FDirectRoomIds;
	QSet<QString> FDisplayNameRequests;
	QSet<QString> FRoomNameRequests;
	QSet<QString> FPendingOlmRecoveryDevices;
	QMap<QString, QPair<QString, QJsonObject>> FPendingForcedOlmEvents;
	QSet<QString> FJoinedMembersRequests;
	QSet<QString> FJoinedMembersLoaded;
	QSet<QString> FDeviceKeyQueries;
	QSet<QString> FAvatarRetries;
	QSet<QString> FAvatarLegacyFallbacks;
	QSet<QString> FAvatarRequestsInFlight;
	QSet<QString> FAvatarUnavailable;
	QSet<QString> FImageRequestsInFlight;
	bool FHasDirectRoomData = false;
	bool FSyncInFlight = false;
	bool FSendInFlight = false;
	bool FKeysUploadInFlight = false;
	bool FKeysUploadCollisionRetried = false;
	bool FInitialSyncComplete = false;
	bool FUseHttp2 = false;
	QMap<QString, QList<MatrixTextEvent>> FMessageHistory;
	QSet<QString> FHistoryLoadedRooms;
	QMap<QString, QString> FRoomPrevBatch;
	QSet<QString> FLimitedRooms;
	QStringList FHighlightBodyPatterns;
	QStringList FHighlightUserPatterns;
	RequestType FInFlightRequest;
	QMap<QString, QJsonObject> FPendingKeyRequests;
	QSet<QString> FRequestedRoomKeys;
	QMap<QString, qint64> FRequestedRoomKeyTimes;
	QMap<QString, QString> FRoomKeyRequestIds;
	QMap<QString, QString> FRoomKeyRequestUsers;
	QMap<QString, QJsonObject> FPendingRoomKeyRequests;
	QMap<QString, QJsonObject> FPendingVerificationMacs;
	QSet<QString> FReadyVerificationMacs;
	QMap<QString, QJsonObject> FVerificationKeyQueryResponses;
	QString FSsssRecoveryInput;
	QString FSsssKeyId;
	QJsonObject FSsssKeyDescription;
	QByteArray FSsssDerivedKey;
	QMap<QString, QByteArray> FSsssSecrets;
	QSet<QString> FSsssPendingSecrets;
	QMap<QString, QString> FSsssSecretRequestIds;
	QMap<QString, QJsonArray> FPendingEncryptedMessages;
	QMap<QString, QString> FSasStates;
	QMap<QString, quint64> FSasTimeoutGenerations;
	QMap<QString, QByteArray> FSasPublicKeys;
	QMap<QString, QByteArray> FSasTheirPublicKeys;
	QMap<QString, QJsonObject> FSasStartContents;
	QMap<QString, QByteArray> FSasCommitments;
	QMap<QString, QString> FSasInitiatorUsers;
	QMap<QString, QString> FSasInitiatorDevices;
	QMap<QString, QString> FSasPeerDevices;
	bool FE2EESyncDiagnosticEmitted = false;

	QNetworkAccessManager *FNetworkAccessManager;
	QObject *FDatabaseWorker = nullptr;
};

#endif // MATRIXNETWORK_H