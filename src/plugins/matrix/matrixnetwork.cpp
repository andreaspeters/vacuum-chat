#include "matrixnetwork.h"
#include "matrixdatabaseworker.h"
#include "matrixssss.h"
#include "interfaces/iprotocolpresence.h"
#include "interfaces/ichatstates.h"
#include "interfaces/matrixreply.h"
#include <utils/imageloadscheduler.h>
#include <QRandomGenerator>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QUrl>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QVariantMap>
#include <QTimer>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QPointer>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QImage>
#include <QUrlQuery>
#include <algorithm>

MatrixNetwork::MatrixNetwork(QObject *parent)
	: QObject(parent), FNetworkAccessManager(nullptr), FAccesToken(), FSyncToken(), FInFlightRequest(RequestNone), FNormalizedServerUrl()
{
	qRegisterMetaType<MatrixPublicRooms::Result>();
	FNetworkAccessManager = new QNetworkAccessManager(this);
	connect(FNetworkAccessManager, &QNetworkAccessManager::finished,
		this, &MatrixNetwork::onReplyFinished);
	connect(this, &MatrixNetwork::oneTimeKeyReceived, this,
		[this](const QString &userId, const QString &deviceId, const QString &, const QString &key) {
			QByteArray keysJson;
			bool keysLoaded = false;
			runDatabase([&](MatrixDatabase &database) {
				keysLoaded = database.loadDeviceKeys(userId, deviceId, keysJson);
			});
			if (!keysLoaded)
				return;
			const QJsonObject keys = QJsonDocument::fromJson(keysJson).object()
				.value(QStringLiteral("keys")).toObject();
			const QString identityKey = keys.value(QStringLiteral("curve25519:%1")
				.arg(deviceId)).toString();
			QString sessionId;
			QByteArray sessionPickle;
			const bool sessionCreated = !identityKey.isEmpty() && !key.isEmpty() &&
				FOlmCrypto.createOlmSessionData(userId, deviceId, identityKey.toUtf8(),
					key.toUtf8(), sessionId, sessionPickle);
			bool sessionSaved = false;
			if (sessionCreated)
				runDatabase([&](MatrixDatabase &database) {
					sessionSaved = database.saveOlmSession(userId, deviceId, sessionId, sessionPickle);
				});
			if (sessionCreated && sessionSaved) {
				const QString recoveryKey = userId + QLatin1Char('\n') + deviceId;
				const auto forcedEvent = FPendingForcedOlmEvents.take(recoveryKey);
				if (!forcedEvent.first.isEmpty())
					sendEncryptedToDeviceEvent(userId, deviceId, forcedEvent.first,
						forcedEvent.second);
				for (auto it = FPendingKeyRequests.constBegin(); it != FPendingKeyRequests.constEnd(); ++it) {
					if (it.value().value(QStringLiteral("sender")).toString() == userId &&
						it.value().value(QStringLiteral("device_id")).toString() == deviceId) {
						sendForwardedRoomKey(userId, deviceId,
							it.value().value(QStringLiteral("room_id")).toString(),
							it.value().value(QStringLiteral("session_id")).toString(), it.key());
					}
			}
			const auto pendingRoomKeyRequests = FPendingRoomKeyRequests;
			for (auto it = pendingRoomKeyRequests.constBegin(); it != pendingRoomKeyRequests.constEnd(); ++it) {
				const QJsonObject pending = it.value();
				if (pending.value(QStringLiteral("target_user")).toString() != userId ||
					pending.value(QStringLiteral("target_device_id")).toString() != deviceId)
					continue;
				FPendingRoomKeyRequests.remove(it.key());
				FRequestedRoomKeys.remove(it.key());
				FRequestedRoomKeyTimes.remove(it.key());
				requestMissingRoomKey(pending.value(QStringLiteral("sender")).toString(),
					pending.value(QStringLiteral("sender_key")).toString(),
					pending.value(QStringLiteral("room_id")).toString(),
					pending.value(QStringLiteral("session_id")).toString());
			}
			const QMap<QString, QJsonArray> pendingMessages = FPendingEncryptedMessages;
				FPendingEncryptedMessages.clear();
				for (auto pendingIt = pendingMessages.constBegin(); pendingIt != pendingMessages.constEnd(); ++pendingIt) {
					for (const QJsonValue &value : pendingIt.value()) {
						const QJsonObject item = value.toObject();
						sendRoomEvent(item.value(QStringLiteral("room_id")).toString(),
							item.value(QStringLiteral("event_type")).toString(QStringLiteral("m.room.message")),
							item.value(QStringLiteral("content")).toObject(),
							item.value(QStringLiteral("transaction_id")).toString());
					}
				}
			}
		});
}

void MatrixNetwork::setDatabaseWorker(QObject *worker)
{
	FDatabaseWorker = worker;
}

void MatrixNetwork::runDatabase(const std::function<void(MatrixDatabase &)> &operation)
{
	if (!FDatabaseWorker || !operation)
		return;
	MatrixDatabaseWorker *worker = qobject_cast<MatrixDatabaseWorker *>(FDatabaseWorker);
	if (!worker)
		return;
	QMetaObject::invokeMethod(worker,
		[worker, operation]() { worker->execute(operation); },
		Qt::BlockingQueuedConnection);
}

bool MatrixNetwork::validateAndNormalizeServerUrl(const QString &serverUrl, QString &normalized) const
{
	if (serverUrl.trimmed().isEmpty()) {
		return false;
	}
	
	// Remove whitespace
	QString url = serverUrl.trimmed();
	
	// Normalize URL: ensure it's fully qualified
	// If it's just a hostname, prefix with https://
	if (!url.contains("://")) {
		// Extract hostname and optional port
		QString hostname;
		int portPos = url.indexOf(':');
		if (portPos > 0 && portPos < url.length() - 1) {
			hostname = url.left(portPos);
			QString portStr = url.mid(portPos + 1);
			bool ok;
			int port = portStr.toInt(&ok);
			if (ok && port > 0 && port <= 65535) {
				normalized = QString("https://%1:%2").arg(hostname, portStr);
				return true;
			}
		}
		normalized = "https://" + url;
	} else {
		normalized = url;
	}
	
	// Validate URL structure
	QUrl testUrl(normalized);
	if (!testUrl.isValid() || testUrl.scheme().isEmpty()) {
		return false;
	}
	
	// Only allow http and https schemes
	QString scheme = testUrl.scheme().toLower();
	if (scheme != "http" && scheme != "https") {
		return false;
	}
	
	// Ensure host is present
	if (testUrl.host().isEmpty()) {
		return false;
	}
	
	return true;
}

void MatrixNetwork::setServerUrl(const QString &serverUrl)
{
	QString normalized;
	if (!validateAndNormalizeServerUrl(serverUrl, normalized)) {
		qWarning() << "Invalid Matrix server URL:" << serverUrl;
		return;
	}
	
	FNormalizedServerUrl = normalized;
	QString baseUrl = normalized;
	
	// Add trailing slash if not present
	if (!baseUrl.endsWith('/')) {
		baseUrl += '/';
	}
	
	FServerUrl = baseUrl;
	emit connectionStateChanged(FAccesToken.isEmpty() ? 0 : 2);
}

void MatrixNetwork::setDatabaseProfileDirectory(const QString &profileDirectory)
{
	FDatabaseProfileDirectory = profileDirectory;
}

void MatrixNetwork::requestSsssRecovery(const QString &passphraseOrRecoveryKey)
{
	if (FAccesToken.isEmpty() || FUserId.isEmpty() || passphraseOrRecoveryKey.isEmpty()) {
		emit ssssRecoveryFinished(false, QStringLiteral("Matrix SSSS recovery requires an active login and input"));
		return;
	}
	FSsssRecoveryInput = passphraseOrRecoveryKey;
	FSsssKeyId.clear();
	FSsssKeyDescription = QJsonObject();
	FSsssDerivedKey.clear();
	FSsssSecrets.clear();
	FSsssPendingSecrets = {QStringLiteral("m.cross_signing.master"),
		QStringLiteral("m.cross_signing.self_signing"),
		QStringLiteral("m.cross_signing.user_signing")};
	QNetworkRequest request(QUrl(constructUrl(QStringLiteral("/_matrix/client/v3/user/%1/account_data/m.secret_storage.default_key")
		.arg(QUrl::toPercentEncoding(FUserId)))));
	request.setAttribute(QNetworkRequest::Http2AllowedAttribute, FUseHttp2);
	request.setRawHeader("Authorization", QByteArray("Bearer ") + FAccesToken.toUtf8());
	QNetworkReply *reply = FNetworkAccessManager->get(request);
	reply->setProperty("requestType", QStringLiteral("ssss_default"));
}

void MatrixNetwork::requestSsssRecoveryFromVerifiedDevices()
{
	if (FAccesToken.isEmpty() || FUserId.isEmpty()) {
		emit ssssRecoveryFinished(false, QStringLiteral("SSSS recovery requires an active login"));
		return;
	}
	FSsssRecoveryInput.clear();
	FSsssKeyId.clear();
	FSsssKeyDescription = QJsonObject();
	FSsssDerivedKey.clear();
	FSsssSecrets.clear();
	FSsssPendingSecrets = {QStringLiteral("m.cross_signing.master"),
		QStringLiteral("m.cross_signing.self_signing"),
		QStringLiteral("m.cross_signing.user_signing")};
	const QSet<QString> pendingSecrets = FSsssPendingSecrets;
	for (const QString &secret : pendingSecrets)
		requestSsssSecretFromDevices(secret);
}

QByteArray MatrixNetwork::takeSsssSecret(const QString &secretName)
{
	return FSsssSecrets.value(secretName);
}

bool MatrixNetwork::importRoomKeyFile(const QString &filePath, const QString &passphrase, QString &error)
{
	if (filePath.isEmpty() || passphrase.isEmpty()) {
		error = QStringLiteral("File path and passphrase are required");
		return false;
	}
	QFile file(filePath);
	if (!file.open(QIODevice::ReadOnly)) {
		error = file.errorString();
		return false;
	}
	QJsonDocument sessions;
	if (!MatrixSsss::decryptExportedSessions(file.readAll(), passphrase, sessions, error))
		return false;
	const QJsonArray exported = sessions.isArray()
		? sessions.array()
		: sessions.object().value(QStringLiteral("sessions")).toArray();
	if (exported.isEmpty()) {
		error = QStringLiteral("The export contains no room-key sessions");
		return false;
	}
	int imported = 0;
	for (const QJsonValue &value : exported) {
		const QJsonObject session = value.toObject();
		const QString roomId = session.value(QStringLiteral("room_id")).toString();
		const QString sessionId = session.value(QStringLiteral("session_id")).toString();
		const QByteArray sessionKey = session.value(QStringLiteral("session_key")).toString().toUtf8();
		const QString senderKey = session.value(QStringLiteral("sender_key")).toString();
		if (roomId.isEmpty() || sessionId.isEmpty() || sessionKey.isEmpty() || senderKey.isEmpty()) {
			qWarning() << "[Matrix-E2EE] skipped exported room key with missing fields:"
				<< "room_id:" << roomId << "session_id:" << sessionId
				<< "session_key_bytes:" << sessionKey.size() << "sender_key:" << !senderKey.isEmpty();
			continue;
		}
		QByteArray pickle;
		if (!FOlmCrypto.prepareExportedMegolmSession(sessionId, sessionKey, pickle))
			continue;
		const QJsonObject claimedKeys = session.value(QStringLiteral("sender_claimed_keys")).toObject();
		const QString claimedEd25519 = claimedKeys.value(QStringLiteral("ed25519")).toString();
		const QString chain = QString::fromUtf8(QJsonDocument(session.value(
			QStringLiteral("forwarding_curve25519_key_chain")).toArray())
			.toJson(QJsonDocument::Compact));
		bool saved = false;
		runDatabase([&](MatrixDatabase &database) {
			saved = database.saveMegolmSession(sessionId, roomId, senderKey, pickle,
				claimedEd25519, chain);
		});
		if (saved) {
			++imported;
			retryPendingEncryptedEvents(roomId, sessionId);
		}
	}
	if (imported == 0) {
		error = QStringLiteral("No valid room-key sessions were imported");
		return false;
	}
	qWarning() << "[Matrix-E2EE] imported room-key sessions:" << imported;
	return true;
}

void MatrixNetwork::uploadOwnDeviceSignature(const QByteArray &selfSigningSeed)
{
	if (selfSigningSeed.isEmpty() || FUserId.isEmpty() || FDeviceId.isEmpty() || FAccesToken.isEmpty())
		return;
	QByteArray deviceKeysJson;
	bool loaded = false;
	runDatabase([&](MatrixDatabase &database) {
		loaded = database.loadDeviceKeys(FUserId, FDeviceId, deviceKeysJson);
	});
	if (!loaded)
		return;
	QJsonObject deviceKeys = QJsonDocument::fromJson(deviceKeysJson).object();
	if (deviceKeys.value(QStringLiteral("user_id")).toString() != FUserId ||
		deviceKeys.value(QStringLiteral("device_id")).toString() != FDeviceId)
		return;
	QJsonObject signedDevice = deviceKeys;
	signedDevice.remove(QStringLiteral("signatures"));
	signedDevice.remove(QStringLiteral("unsigned"));
	QByteArray selfSigningPublic;
	const QByteArray signature = FOlmCrypto.signWithPkSeed(selfSigningSeed,
		FOlmCrypto.canonicalJson(signedDevice), selfSigningPublic);
	if (signature.isEmpty() || selfSigningPublic.isEmpty())
		return;
	QByteArray selfSigningJson;
	bool selfSigningLoaded = false;
	runDatabase([&](MatrixDatabase &database) {
		selfSigningLoaded = database.loadCrossSigningKey(FUserId,
			QStringLiteral("self_signing"), selfSigningJson);
	});
	const QJsonObject selfSigning = selfSigningLoaded
		? QJsonDocument::fromJson(selfSigningJson).object() : QJsonObject();
	const QJsonObject selfSigningKeys = selfSigning.value(QStringLiteral("keys")).toObject();
	if (selfSigningKeys.size() != 1 ||
		selfSigningKeys.constBegin().value().toString() != QString::fromLatin1(selfSigningPublic))
		return;
	QJsonObject signatures = deviceKeys.value(QStringLiteral("signatures")).toObject();
	QJsonObject userSignatures = signatures.value(FUserId).toObject();
	userSignatures.insert(QStringLiteral("ed25519:%1").arg(QString::fromLatin1(selfSigningPublic)),
		QString::fromLatin1(signature));
	signedDevice.insert(QStringLiteral("signatures"), QJsonObject{{FUserId, userSignatures}});
	const QJsonObject payload{{FUserId, QJsonObject{{FDeviceId, signedDevice}}}};
	QNetworkRequest request(QUrl(constructUrl(QStringLiteral("/_matrix/client/v3/keys/signatures/upload"))));
	request.setAttribute(QNetworkRequest::Http2AllowedAttribute, FUseHttp2);
	request.setRawHeader("Authorization", QByteArray("Bearer ") + FAccesToken.toUtf8());
	request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
	QNetworkReply *reply = FNetworkAccessManager->post(request,
		QJsonDocument(payload).toJson(QJsonDocument::Compact));
	reply->setProperty("requestType", QStringLiteral("signatures_upload"));
}

void MatrixNetwork::uploadRecoveredMasterKeySignature()
{
	if (FUserId.isEmpty() || FDeviceId.isEmpty() || FAccesToken.isEmpty())
		return;
	QByteArray masterJson;
	bool loaded = false;
	runDatabase([&](MatrixDatabase &database) {
		loaded = database.loadCrossSigningKey(FUserId, QStringLiteral("master"), masterJson);
	});
	if (!loaded)
		masterJson = QJsonDocument(FVerificationKeyQueryResponses.value(FUserId + QLatin1Char('\n'))
			.value(QStringLiteral("master_keys")).toObject().value(FUserId).toObject())
			.toJson(QJsonDocument::Compact);
	const QJsonObject master = QJsonDocument::fromJson(masterJson).object();
	if (!master.isEmpty())
		uploadOwnMasterKeySignature(master);
}

void MatrixNetwork::uploadUserMasterSignature(const QString &userId,
                                               const QByteArray &userSigningSeed)
{
	if (userId.isEmpty() || userId == FUserId || userSigningSeed.isEmpty() || FAccesToken.isEmpty())
		return;
	QByteArray masterJson;
	bool loaded = false;
	runDatabase([&](MatrixDatabase &database) {
		loaded = database.loadCrossSigningKey(userId, QStringLiteral("master"), masterJson);
	});
	QJsonObject master = loaded ? QJsonDocument::fromJson(masterJson).object() : QJsonObject();
	if (master.isEmpty())
		master = FVerificationKeyQueryResponses.value(userId + QLatin1Char('\n'))
			.value(QStringLiteral("master_keys")).toObject().value(userId).toObject();
	if (master.value(QStringLiteral("user_id")).toString() != userId)
		return;
	const QJsonObject keys = master.value(QStringLiteral("keys")).toObject();
	if (keys.size() != 1)
		return;
	QJsonObject signedMaster = master;
	signedMaster.remove(QStringLiteral("signatures"));
	signedMaster.remove(QStringLiteral("unsigned"));
	QByteArray signingPublic;
	const QByteArray signature = FOlmCrypto.signWithPkSeed(userSigningSeed,
		QJsonDocument(signedMaster).toJson(QJsonDocument::Compact), signingPublic);
	if (signature.isEmpty() || signingPublic.isEmpty())
		return;
	QByteArray userSigningJson;
	bool userSigningLoaded = false;
	runDatabase([&](MatrixDatabase &database) {
		userSigningLoaded = database.loadCrossSigningKey(FUserId,
			QStringLiteral("user_signing"), userSigningJson);
	});
	const QJsonObject userSigning = userSigningLoaded
		? QJsonDocument::fromJson(userSigningJson).object() : QJsonObject();
	const QJsonObject userSigningKeys = userSigning.value(QStringLiteral("keys")).toObject();
	if (userSigningKeys.size() != 1 ||
		userSigningKeys.constBegin().value().toString() != QString::fromLatin1(signingPublic))
		return;
	QJsonObject localSignatures;
	localSignatures.insert(QStringLiteral("ed25519:%1").arg(QString::fromLatin1(signingPublic)),
		QString::fromLatin1(signature));
	signedMaster.insert(QStringLiteral("signatures"), QJsonObject{{FUserId, localSignatures}});
	const QString masterValue = keys.constBegin().value().toString();
	const QJsonObject payload{{userId, QJsonObject{{masterValue, signedMaster}}}};
	QNetworkRequest request(QUrl(constructUrl(QStringLiteral("/_matrix/client/v3/keys/signatures/upload"))));
	request.setAttribute(QNetworkRequest::Http2AllowedAttribute, FUseHttp2);
	request.setRawHeader("Authorization", QByteArray("Bearer ") + FAccesToken.toUtf8());
	request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
	QNetworkReply *reply = FNetworkAccessManager->post(request,
		QJsonDocument(payload).toJson(QJsonDocument::Compact));
	reply->setProperty("requestType", QStringLiteral("signatures_upload"));
}

void MatrixNetwork::requestSsssSecretFromDevices(const QString &secretName)
{
	if (FUserId.isEmpty() || secretName.isEmpty())
		return;
	QString requestId = FSsssSecretRequestIds.value(secretName);
	if (requestId.isEmpty()) {
		requestId = QStringLiteral("ssss.") + generateTransactionId();
		FSsssSecretRequestIds.insert(secretName, requestId);
	}
	const QJsonObject content{
		{QStringLiteral("action"), QStringLiteral("request")},
		{QStringLiteral("name"), secretName},
		{QStringLiteral("request_id"), requestId},
		{QStringLiteral("requesting_device_id"), FDeviceId}};
	const QJsonObject body{{QStringLiteral("messages"),
		QJsonObject{{FUserId, QJsonObject{{QStringLiteral("*"), content}}}}}};
	QNetworkRequest request(QUrl(constructUrl(QStringLiteral("/_matrix/client/v3/sendToDevice/m.secret.request/%1")
		.arg(QUrl::toPercentEncoding(generateTransactionId())))));
	request.setAttribute(QNetworkRequest::Http2AllowedAttribute, FUseHttp2);
	request.setRawHeader("Authorization", QByteArray("Bearer ") + FAccesToken.toUtf8());
	request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
	QNetworkReply *reply = FNetworkAccessManager->put(request,
		QJsonDocument(body).toJson(QJsonDocument::Compact));
	reply->setProperty("requestType", QStringLiteral("to_device"));
	reply->setProperty("e2eeEventType", QStringLiteral("m.secret.request"));
}

bool MatrixNetwork::validateRecoveredMasterSecret()
{
	const QByteArray masterSeed = FSsssSecrets.value(QStringLiteral("m.cross_signing.master"));
	if (masterSeed.isEmpty())
		return false;
	QByteArray publicKey;
	if (FOlmCrypto.signWithPkSeed(masterSeed, QByteArrayLiteral("{}"), publicKey).isEmpty())
		return false;
	QByteArray masterJson;
	bool loaded = false;
	runDatabase([&](MatrixDatabase &database) {
		loaded = database.loadCrossSigningKey(FUserId, QStringLiteral("master"), masterJson);
	});
	QJsonObject master = loaded ? QJsonDocument::fromJson(masterJson).object() : QJsonObject();
	if (master.isEmpty())
		master = FVerificationKeyQueryResponses.value(FUserId + QLatin1Char('\n'))
			.value(QStringLiteral("master_keys")).toObject().value(FUserId).toObject();
	const QJsonObject keys = master.value(QStringLiteral("keys")).toObject();
	const bool valid = keys.size() == 1 &&
		keys.constBegin().value().toString() == QString::fromLatin1(publicKey);
	if (valid)
		runDatabase([&](MatrixDatabase &database) {
			database.saveCrossSigningKey(FUserId, QStringLiteral("master"),
				QJsonDocument(master).toJson(QJsonDocument::Compact));
		});
	return valid;
}

bool MatrixNetwork::isOwnDeviceCrossSigningVerified(const QString &deviceId) const
{
	if (deviceId.isEmpty() || FUserId.isEmpty())
		return false;
	QByteArray masterJson;
	QByteArray selfSigningJson;
	QByteArray deviceJson;
	bool loaded = false;
	const_cast<MatrixNetwork *>(this)->runDatabase([&](MatrixDatabase &database) {
		loaded = database.loadCrossSigningKey(FUserId, QStringLiteral("master"), masterJson) &&
			database.loadCrossSigningKey(FUserId, QStringLiteral("self_signing"), selfSigningJson) &&
			database.loadDeviceKeys(FUserId, deviceId, deviceJson);
	});
	if (!loaded)
		return false;
	const QJsonObject master = QJsonDocument::fromJson(masterJson).object();
	const QJsonObject selfSigning = QJsonDocument::fromJson(selfSigningJson).object();
	const QJsonObject device = QJsonDocument::fromJson(deviceJson).object();
	const QJsonObject masterKeys = master.value(QStringLiteral("keys")).toObject();
	const QJsonObject selfKeys = selfSigning.value(QStringLiteral("keys")).toObject();
	if (masterKeys.size() != 1 || selfKeys.size() != 1)
		return false;
	return FOlmCrypto.verifyCrossSigningKey(FUserId, selfSigning,
		masterKeys.constBegin().key(), masterKeys.constBegin().value().toString()) &&
		FOlmCrypto.verifyDeviceCrossSignature(FUserId, device,
			selfKeys.constBegin().key(), selfKeys.constBegin().value().toString());
}

void MatrixNetwork::requestSsssKeyDescription(const QString &keyId)
{
	QNetworkRequest request(QUrl(constructUrl(QStringLiteral("/_matrix/client/v3/user/%1/account_data/m.secret_storage.key.%2")
		.arg(QUrl::toPercentEncoding(FUserId), QUrl::toPercentEncoding(keyId)))));
	request.setAttribute(QNetworkRequest::Http2AllowedAttribute, FUseHttp2);
	request.setRawHeader("Authorization", QByteArray("Bearer ") + FAccesToken.toUtf8());
	QNetworkReply *reply = FNetworkAccessManager->get(request);
	reply->setProperty("requestType", QStringLiteral("ssss_key"));
}

void MatrixNetwork::requestSsssSecret(const QString &secretName)
{
	QNetworkRequest request(QUrl(constructUrl(QStringLiteral("/_matrix/client/v3/user/%1/account_data/%2")
		.arg(QUrl::toPercentEncoding(FUserId), QUrl::toPercentEncoding(secretName)))));
	request.setAttribute(QNetworkRequest::Http2AllowedAttribute, FUseHttp2);
	request.setRawHeader("Authorization", QByteArray("Bearer ") + FAccesToken.toUtf8());
	QNetworkReply *reply = FNetworkAccessManager->get(request);
	reply->setProperty("requestType", QStringLiteral("ssss_secret"));
	reply->setProperty("ssssName", secretName);
}

void MatrixNetwork::setActiveRoom(const QString &roomId)
{
	const QString normalizedRoomId = roomId.trimmed();
	if (FActiveRoomId == normalizedRoomId) {
		queryDeviceKeysForRoom(normalizedRoomId);
		return;
	}
	FActiveRoomId = normalizedRoomId;
	queryDeviceKeysForRoom(FActiveRoomId);
	if (!FActiveRoomId.isEmpty())
		retryPendingEncryptedEvents(FActiveRoomId, QString());
}

void MatrixNetwork::queryDeviceKeysForRoom(const QString &roomId)
{
	if (roomId.isEmpty())
		return;
	const auto roomIt = FRooms.constFind(roomId);
	if (roomIt == FRooms.constEnd() || !roomIt->isEncrypted)
		return;
	for (const ProtocolRosterEntry &member : roomIt->members) {
		if (!member.isValid || member.id == FUserId || FDeviceKeyQueries.contains(member.id))
			continue;
		QMap<QString, QByteArray> cachedKeys;
		runDatabase([&](MatrixDatabase &database) {
			cachedKeys = database.loadDeviceKeysForUser(member.id);
		});
		if (!cachedKeys.isEmpty()) {
			FDeviceKeyQueries.insert(member.id);
			qWarning() << "[Matrix-E2EE] loaded cached member device keys for active room:"
				<< roomId << member.id << cachedKeys.size();
			continue;
		}
		FDeviceKeyQueries.insert(member.id);
		queryDeviceKeys(member.id);
	}
}

MatrixNetwork::RequestType MatrixNetwork::currentRequestType() const
{
	return FInFlightRequest;
}

bool MatrixNetwork::isRequestInProgress() const
{
	return FInFlightRequest != RequestNone;
}

void MatrixNetwork::scheduleSyncRetry(int delayMs)
{
	QTimer::singleShot(delayMs, this, [this]() {
		if (!FAccesToken.isEmpty() && !FSyncInFlight)
			sync();
	});
}

void MatrixNetwork::setRequestType(RequestType type)
{
	FInFlightRequest = type;
}

QString MatrixNetwork::constructUrl(const QString &path) const
{
	if (FServerUrl.isEmpty()) {
		qWarning() << "Cannot construct URL without server URL set";
		return QString();
	}
	
	QString baseUrl = FServerUrl;
	if (!baseUrl.endsWith('/')) {
		baseUrl += '/';
	}
	QString normalizedPath = path.trimmed();
	while (normalizedPath.startsWith('/'))
		normalizedPath.remove(0, 1);
	return baseUrl + normalizedPath;
}

QString MatrixNetwork::generateTransactionId() const
{
	return QString("m_%1").arg(QDateTime::currentMSecsSinceEpoch());
}

QString MatrixNetwork::login(const QString &userId, const QString &password, const QString &deviceId)
{
	// Prevent concurrent login requests
	if (isRequestInProgress())
	{
		qInfo() << "Matrix login request already active; ignoring duplicate invocation";
		return QString();
	}
	
	if (userId.trimmed().isEmpty() || password.trimmed().isEmpty())
	{
		qWarning() << "MatrixNetwork::login: empty username or password";
		emit loginError("Username and password cannot be empty");
		return QString();
	}
	
	if (FNormalizedServerUrl.isEmpty())
	{
		qWarning() << "MatrixNetwork::login: server URL not configured";
		emit loginError("Server URL not configured");
		return QString();
	}
	
	QString loginPath = "/_matrix/client/v3/login";
	QUrl loginUrl(constructUrl(loginPath));
	QNetworkRequest request(loginUrl);
	request.setAttribute(QNetworkRequest::Http2AllowedAttribute, FUseHttp2);
	request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

	// Normalize userId: strip leading '@' if present and remove ':server' suffix
	QString normalizedUser = userId;
	if (normalizedUser.startsWith('@'))
		normalizedUser.remove(0, 1);  // Strip leading '@'
	if (normalizedUser.contains(':'))
		normalizedUser = normalizedUser.section(':', 0, 0);  // Remove ':server' suffix

	// Reject empty normalized localpart
	if (normalizedUser.trimmed().isEmpty()) {
		qWarning() << "MatrixNetwork::login: empty normalized user ID after stripping @ and :server";
		emit loginError("Invalid user ID");
		return QString();
	}

	QJsonObject body;
	body.insert("type", "m.login.password");
	if (!deviceId.trimmed().isEmpty())
		body.insert("device_id", deviceId.trimmed());
	// Match gomatrix's proven login request: use the localpart in the
	// legacy top-level user field.
	body.insert("user", normalizedUser);
	body.insert("password", password);  // Note: Password is NOT logged

	QNetworkReply *reply = FNetworkAccessManager->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
	QTimer::singleShot(30000, reply, [reply]() {
		if (reply->isRunning())
			reply->abort();
	});
	connect(reply, &QNetworkReply::errorOccurred, this, [loginUrl](QNetworkReply::NetworkError error) {
		qWarning() << "Matrix login network error:" << error << "url:" << loginUrl.toString();
	});
#if QT_CONFIG(ssl)
	connect(reply, &QNetworkReply::sslErrors, this, [loginUrl](const QList<QSslError> &errors) {
		qWarning() << "Matrix login SSL errors for:" << loginUrl.toString() << "count:" << errors.size();
	});
#endif

	reply->setProperty("requestType", "login");
	setRequestType(RequestLogin);
	emit connectionStateChanged(1);  // Connecting state
	return QString();
}

void MatrixNetwork::loginWithAccessToken(const QString &userId, const QString &accessToken,
	const QString &deviceId)
{
	if (userId.trimmed().isEmpty() || accessToken.trimmed().isEmpty() || FNormalizedServerUrl.isEmpty()) {
		emit loginError(QStringLiteral("Matrix access-token login requires user, token and server"));
		return;
	}
	FAccesToken = accessToken.trimmed();
	FUserId = userId.trimmed();
	FDeviceId = deviceId.trimmed();
	FInitialSyncComplete = false;
	FKeysUploadInFlight = false;
	bool databaseOpened = false;
	if (FDatabaseWorker) {
		QMetaObject::invokeMethod(FDatabaseWorker, "openForAccount", Qt::BlockingQueuedConnection,
			Q_RETURN_ARG(bool, databaseOpened),
			Q_ARG(QString, FDatabaseProfileDirectory), Q_ARG(QString, FNormalizedServerUrl),
			Q_ARG(QString, FUserId));
	}
	if (!databaseOpened) {
		FAccesToken.clear();
		emit loginError(QStringLiteral("Matrix SQLite database could not be opened"));
		return;
	}
	bool localOlmContextChanged = false;
	bool localOlmContextKnown = false;
	runDatabase([&](MatrixDatabase &database) {
		localOlmContextKnown = database.ensureOlmSessionContext(FUserId, FDeviceId,
			localOlmContextChanged);
		if (localOlmContextKnown && localOlmContextChanged)
			localOlmContextKnown = database.clearOlmSessionsForUser(FUserId);
	});
	if (!localOlmContextKnown) {
		FAccesToken.clear();
		emit loginError(QStringLiteral("Matrix Olm session context could not be initialized"));
		return;
	}
	if (localOlmContextChanged)
		qWarning() << "[Matrix-E2EE] local device context changed; discarded persisted remote Olm sessions";
	if (qEnvironmentVariableIsSet("VACUUM_MATRIX_CLEAR_SYNC_CURSOR")) {
		bool cleared = false;
		if (FDatabaseWorker)
			QMetaObject::invokeMethod(FDatabaseWorker, "clearNextBatch", Qt::BlockingQueuedConnection,
				Q_RETURN_ARG(bool, cleared));
		Q_UNUSED(cleared);
	}
	if (FDatabaseWorker) {
		QMetaObject::invokeMethod(FDatabaseWorker, "directRooms", Qt::BlockingQueuedConnection,
			Q_RETURN_ARG(QSet<QString>, FDirectRoomIds));
	}
	FHasDirectRoomData = !FDirectRoomIds.isEmpty();
	restorePersistedRooms();
	if (FDatabaseWorker) {
		QMetaObject::invokeMethod(FDatabaseWorker, "nextBatch", Qt::BlockingQueuedConnection,
			Q_RETURN_ARG(QString, FSyncToken));
		QMetaObject::invokeMethod(FDatabaseWorker, "syncFilter", Qt::BlockingQueuedConnection,
			Q_RETURN_ARG(QString, FSyncFilterId));
	}
	FFilterCreationAttempted = false;
	FMessageHistory.clear();
	FHistoryLoadedRooms.clear();
	QByteArray olmPickle;
	QByteArray olmPickleKey;
	bool olmInitialized = false;
	runDatabase([&](MatrixDatabase &database) {
		if (database.loadOlmAccount(FUserId, FDeviceId, olmPickleKey, olmPickle))
			olmInitialized = true;
	});
	if (olmInitialized)
		olmInitialized = FOlmCrypto.initializeFromStoredAccount(FUserId, FDeviceId,
			olmPickleKey, olmPickle);
	else {
		olmInitialized = FOlmCrypto.initializeNewAccount(FUserId, FDeviceId,
			olmPickleKey, olmPickle);
		if (olmInitialized)
			runDatabase([&](MatrixDatabase &database) {
				olmInitialized = database.saveOlmAccount(FUserId, FDeviceId,
					olmPickleKey, olmPickle);
			});
	}
	if (!olmInitialized) {
		FAccesToken.clear();
		emit loginError(QStringLiteral("Matrix Olm account initialization failed"));
		return;
	}
	emit loginSuccess(FUserId, FAccesToken, FDeviceId);
	queryOwnDevices();
	sync();
}

void MatrixNetwork::logout()
{
	// Clear access token and sync state
	if (!FAccesToken.isEmpty() && !FUserId.isEmpty())
		setPresence(FUserId, QStringLiteral("offline"));
	FAccesToken.clear();
	FSsssRecoveryInput.clear();
	FSsssKeyId.clear();
	FSsssKeyDescription = QJsonObject();
	FSsssDerivedKey.clear();
	FSsssSecrets.clear();
	FSsssPendingSecrets.clear();
	FSsssSecretRequestIds.clear();
	FKeysUploadInFlight = false;
	FSyncToken.clear();
	FActiveRoomId.clear();
	FSyncFilterId.clear();
	FFilterCreationAttempted = false;
	FReplacedRoomIds.clear();
	FRooms.clear();
	FDisplayNameRequests.clear();
	FAvatarRetries.clear();
	FAvatarLegacyFallbacks.clear();
	FAvatarRequestsInFlight.clear();
	FAvatarUnavailable.clear();
	FRoomNameRequests.clear();
	FJoinedMembersRequests.clear();
	FJoinedMembersLoaded.clear();
	FDeviceKeyQueries.clear();
	FPendingEncryptedMessages.clear();
	FSasPublicKeys.clear();
	FSasTheirPublicKeys.clear();
	FSasInitiatorUsers.clear();
	FSasInitiatorDevices.clear();
	FSasPeerDevices.clear();
	FSasStates.clear();
	FSasTimeoutGenerations.clear();
	if (FDatabaseWorker)
		QMetaObject::invokeMethod(FDatabaseWorker, "close", Qt::BlockingQueuedConnection);
	setRequestType(RequestNone);
	
	emit connectionStateChanged(0);  // Disconnected state
}

void MatrixNetwork::shutdown()
{
	// This method is invoked in MatrixNetwork's own thread before the object is
	// destroyed. Abort replies first so their finished callbacks cannot outlive
	// the network worker during plugin unload.
	for (QNetworkReply *reply : findChildren<QNetworkReply *>())
		if (reply)
			reply->abort();
	FAccesToken.clear();
	FSsssRecoveryInput.clear();
	FSsssKeyId.clear();
	FSsssKeyDescription = QJsonObject();
	FSsssDerivedKey.clear();
	FSsssSecrets.clear();
	FSsssPendingSecrets.clear();
	FSsssSecretRequestIds.clear();
	FSyncInFlight = false;
	FSendInFlight = false;
	FKeysUploadInFlight = false;
	setRequestType(RequestNone);
}

void MatrixNetwork::setPresence(const QString &userId, const QString &presence,
	const QString &statusMessage)
{
	if (FAccesToken.isEmpty() || userId.isEmpty() || presence.isEmpty())
		return;
	const QString encodedUserId = QString::fromUtf8(QUrl::toPercentEncoding(userId));
	const QString path = QStringLiteral("/_matrix/client/v3/presence/%1/status").arg(encodedUserId);
	QNetworkRequest request(QUrl(constructUrl(path)));
	request.setAttribute(QNetworkRequest::Http2AllowedAttribute, FUseHttp2);
	request.setRawHeader("Authorization", QByteArray("Bearer ") + FAccesToken.toUtf8());
	request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
	QJsonObject content;
	content.insert(QStringLiteral("presence"), presence);
	content.insert(QStringLiteral("status_msg"), statusMessage);
	FNetworkAccessManager->put(request, QJsonDocument(content).toJson(QJsonDocument::Compact));
}

void MatrixNetwork::setTyping(const QString &roomId, bool typing)
{
	if (FAccesToken.isEmpty() || !roomId.startsWith(QLatin1Char('!')) || FUserId.isEmpty())
		return;
	const QString encodedUserId = QString::fromUtf8(QUrl::toPercentEncoding(FUserId));
	const QString encodedRoomId = QString::fromUtf8(QUrl::toPercentEncoding(roomId));
	const QString path = QStringLiteral("/_matrix/client/v3/rooms/%1/typing/%2")
		.arg(encodedRoomId, encodedUserId);
	QNetworkRequest request(QUrl(constructUrl(path)));
	request.setAttribute(QNetworkRequest::Http2AllowedAttribute, FUseHttp2);
	request.setRawHeader("Authorization", QByteArray("Bearer ") + FAccesToken.toUtf8());
	request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
	QJsonObject content;
	content.insert(QStringLiteral("typing"), typing);
	if (typing)
		content.insert(QStringLiteral("timeout"), 30000);
	FNetworkAccessManager->put(request, QJsonDocument(content).toJson(QJsonDocument::Compact));
}

void MatrixNetwork::createSyncFilter(bool roomsOnly)
{
	FFilterCreationAttempted = true;
	const QJsonObject filter{{QStringLiteral("presence"), QJsonObject{{QStringLiteral("limit"), 0}}},
		{QStringLiteral("account_data"), QJsonObject{{QStringLiteral("limit"), 0}}},
		{QStringLiteral("room"), QJsonObject{
			{QStringLiteral("state"), QJsonObject{{QStringLiteral("lazy_load_members"), true},
				{QStringLiteral("include_redundant_members"), false}}},
			{QStringLiteral("timeline"), QJsonObject{{QStringLiteral("limit"), 20}}},
			{QStringLiteral("ephemeral"), QJsonObject{{QStringLiteral("limit"), 0}}},
			{QStringLiteral("account_data"), QJsonObject{{QStringLiteral("limit"), 0}}},
			{QStringLiteral("include_leave"), false}}}};
	const QString userPath = QString::fromUtf8(QUrl::toPercentEncoding(FUserId));
	QNetworkRequest request(QUrl(constructUrl(QStringLiteral("/_matrix/client/v3/user/%1/filter").arg(userPath))));
	request.setAttribute(QNetworkRequest::Http2AllowedAttribute, FUseHttp2);
	request.setRawHeader("Authorization", QByteArray("Bearer ") + FAccesToken.toUtf8());
	request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
	QNetworkReply *reply = FNetworkAccessManager->post(request,
		QJsonDocument(filter).toJson(QJsonDocument::Compact));
	reply->setProperty("requestType", QStringLiteral("filter_create"));
	reply->setProperty("roomsOnly", roomsOnly);
}

void MatrixNetwork::sync(bool roomsOnly)
{

	// Only one long-poll may be active. Sends are independent Matrix requests
	// and must not block or invalidate the sync loop.
	if (FSyncInFlight) {
		qWarning() << "Sync already in progress, rejecting concurrent request";
		emit syncError("Sync already in progress");
		return;
	}
	
	if (FAccesToken.isEmpty()) {
		emit syncError("Access token missing for sync");
		return;
	}
	if (FSyncFilterId.isEmpty() && !FFilterCreationAttempted) {
		createSyncFilter(roomsOnly);
		return;
	}
	
	bool timeoutOk = false;
	const int syncTimeoutMs = qEnvironmentVariable("VACUUM_MATRIX_SYNC_TIMEOUT_MS",
		QStringLiteral("30000")).toInt(&timeoutOk);
	const int effectiveSyncTimeoutMs = timeoutOk && syncTimeoutMs > 0
		? qBound(100, syncTimeoutMs, 120000) : 30000;
	QString syncPath = QStringLiteral("/_matrix/client/v3/sync?timeout=%1")
		.arg(effectiveSyncTimeoutMs);
	const int timelineLimit = roomsOnly ? 0 : 20;
	const QJsonObject syncFilter{
		// Do not set types/not_types/event_fields here: without a user-facing
		// allow-list they would silently discard valid room events. The filter
		// deliberately uses only lossless limits and the explicit include_leave
		// contract; callers can still use the inline JSON filter fallback.
		{QStringLiteral("presence"), QJsonObject{{QStringLiteral("limit"), 0}}},
		{QStringLiteral("account_data"), QJsonObject{{QStringLiteral("limit"), 0}}},
		{QStringLiteral("room"), QJsonObject{
			{QStringLiteral("state"), QJsonObject{
				{QStringLiteral("lazy_load_members"), true},
				{QStringLiteral("include_redundant_members"), false}}},
			{QStringLiteral("timeline"), QJsonObject{{QStringLiteral("limit"), timelineLimit}}},
			{QStringLiteral("ephemeral"), QJsonObject{{QStringLiteral("limit"), 0}}},
			{QStringLiteral("account_data"), QJsonObject{{QStringLiteral("limit"), 0}}},
			{QStringLiteral("include_leave"), false}}}};
	syncPath += QStringLiteral("&set_presence=online&use_state_after=true&filter=");
	if (!FSyncFilterId.isEmpty())
		syncPath += QUrl::toPercentEncoding(FSyncFilterId);
	else
		syncPath += QUrl::toPercentEncoding(QJsonDocument(syncFilter).toJson(QJsonDocument::Compact));
	if (roomsOnly) {
		syncPath += QStringLiteral("&full_state=true");
	}
	if (!FSyncToken.isEmpty()) {
		syncPath += QString("&since=%1").arg(QUrl::toPercentEncoding(FSyncToken));
	}
	
	QUrl syncUrl(constructUrl(syncPath));
	QNetworkRequest request(syncUrl);
	request.setAttribute(QNetworkRequest::Http2AllowedAttribute, FUseHttp2);
	request.setRawHeader("Authorization", QByteArray("Bearer ") + FAccesToken.toUtf8());
	QNetworkReply *reply = FNetworkAccessManager->get(request);
	const int localSyncTimeoutMs = qBound(effectiveSyncTimeoutMs + 10000,
		10000, 130000);
	QTimer::singleShot(localSyncTimeoutMs, reply, [reply]() {
		if (reply->isRunning()) {
			reply->setProperty("localSyncTimeout", true);
			reply->abort();
		}
	});
	reply->setProperty("requestType", "sync");
	reply->setProperty("syncStartedAt", QDateTime::currentMSecsSinceEpoch());
	FSyncInFlight = true;
}

void MatrixNetwork::sendTextMessage(const QString &roomId, const QString &text, const QString &txnId)
{
	sendMessageEvent(roomId, QJsonObject{{"msgtype", "m.text"}, {"body", text}}, txnId);
}

void MatrixNetwork::joinRoom(const QString &roomId)
{
	changeRoomMembership(roomId, QStringLiteral("join"));
}

void MatrixNetwork::searchPublicRooms(const QString &directoryServer, const QString &searchTerm,
	int limit, const QString &since)
{
	MatrixPublicRooms::Result result;
	if (FAccesToken.isEmpty()) {
		result.error = QStringLiteral("Public room search requires a logged-in Matrix account");
		emit publicRoomsReceived(result);
		return;
	}
	const QUrl endpoint(constructUrl(QStringLiteral("/_matrix/client/v3/publicRooms")));
	QNetworkReply *reply = MatrixPublicRooms::requestPublicRooms(FNetworkAccessManager, endpoint,
		FAccesToken.toUtf8(), directoryServer, searchTerm, limit, since, FUseHttp2);
	if (!reply) {
		result.error = QStringLiteral("Could not create Matrix public-room request");
		emit publicRoomsReceived(result);
		return;
	}
	reply->setProperty("requestType", QStringLiteral("public_rooms"));
}

void MatrixNetwork::startDirectChat(const QString &userId)
{
	const QString inviteeUserId = userId.trimmed();
	if (FAccesToken.isEmpty() || !MatrixDirectRoom::isValidUserId(inviteeUserId)) {
		emit directRoomCreated(inviteeUserId, QString(),
			QStringLiteral("A logged-in Matrix account and a valid Matrix user ID are required"));
		return;
	}
	const QUrl endpoint(constructUrl(QStringLiteral("/_matrix/client/v3/createRoom")));
	QNetworkReply *reply = MatrixDirectRoom::requestCreation(FNetworkAccessManager, endpoint,
		FAccesToken.toUtf8(), inviteeUserId, FUseHttp2, FOlmCrypto.isInitialized());
	if (!reply) {
		emit directRoomCreated(inviteeUserId, QString(),
			QStringLiteral("Could not create Matrix direct-room request"));
		return;
	}
	reply->setProperty("requestType", QStringLiteral("create_direct_room"));
	reply->setProperty("directUserId", inviteeUserId);
}

void MatrixNetwork::leaveRoom(const QString &roomId)
{
	changeRoomMembership(roomId, QStringLiteral("leave"));
}

void MatrixNetwork::changeRoomMembership(const QString &roomId, const QString &action)
{
	if (roomId.isEmpty() || FAccesToken.isEmpty() || (action != QStringLiteral("join") &&
		action != QStringLiteral("leave")))
		return;
	const QString path = QStringLiteral("/_matrix/client/v3/rooms/%1/%2")
		.arg(QString::fromUtf8(QUrl::toPercentEncoding(roomId)), action);
	QNetworkRequest request(QUrl(constructUrl(path)));
	request.setAttribute(QNetworkRequest::Http2AllowedAttribute, FUseHttp2);
	request.setRawHeader("Authorization", QByteArray("Bearer ") + FAccesToken.toUtf8());
	request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
	QNetworkReply *reply = FNetworkAccessManager->post(request, QByteArray("{}"));
	reply->setProperty("requestType", QStringLiteral("room_membership"));
	reply->setProperty("roomId", roomId);
	reply->setProperty("membershipAction", action);
}

void MatrixNetwork::retryFailedOutbox()
{
	QList<MatrixOutboxEntry> failedEntries;
	runDatabase([&](MatrixDatabase &database) {
		failedEntries = database.failedOutboxMessages();
	});
	for (const MatrixOutboxEntry &entry : failedEntries)
		sendTextMessage(entry.roomId, entry.body, entry.transactionId);
}

void MatrixNetwork::requestAvatar(const QString &key, const QString &mxcUrl)
{
	const QUrl mxc(mxcUrl);
	const QString requestKey = key + QChar('\n') + mxcUrl;
	if (key.isEmpty() || mxc.scheme() != QStringLiteral("mxc") || mxc.host().isEmpty() ||
		mxc.path().isEmpty() || FAvatarRequestsInFlight.contains(requestKey))
		return;
	QString profileDirectory;
	runDatabase([&](MatrixDatabase &database) {
		profileDirectory = database.profileDirectory();
	});
	const QString cacheDirectory = profileDirectory.isEmpty()
		? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/matrix-avatars")
		: profileDirectory + QStringLiteral("/avatars");
	QDir().mkpath(cacheDirectory);
	const QString cachePath = cacheDirectory + QLatin1Char('/') +
		QString::fromLatin1(QCryptographicHash::hash(mxcUrl.toUtf8(), QCryptographicHash::Sha256).toHex()) +
		QStringLiteral(".bin");
	FAvatarRequestsInFlight.insert(requestKey);
	QPointer<MatrixNetwork> network(this);
	ImageLoadScheduler *scheduler = ImageLoadScheduler::instance();
	if (!scheduler) {
		FAvatarRequestsInFlight.remove(requestKey);
		return;
	}
	scheduler->submit(ImageLoadScheduler::fileKey(cachePath),
		[network, key, mxcUrl, requestKey, cachePath](ImageLoadScheduler::Completion done) {
			if (!network) {
				done(QImage());
				return;
			}
			ImageLoadScheduler *queue = ImageLoadScheduler::instance();
			if (!queue) {
				done(QImage());
				return;
			}
			connect(network, &QObject::destroyed, queue, [done]() { done(QImage()); });
			queue->decodeFileAsync(cachePath,
				[network, key, mxcUrl, requestKey, cachePath, done](const QImage &cachedImage) {
					if (!network) {
						done(QImage());
						return;
					}
					if (!cachedImage.isNull()) {
						done(cachedImage);
						return;
					}
					QFile::remove(cachePath);
					QMetaObject::invokeMethod(network,
						[network, key, mxcUrl, requestKey, cachePath, done]() {
							if (!network) {
								done(QImage());
								return;
							}
							if (network->FAvatarUnavailable.contains(requestKey) ||
								network->FAccesToken.isEmpty()) {
								done(QImage());
								return;
							}
							const QUrl mxc(mxcUrl);
							const QString mediaId = mxc.path().mid(1);
							const bool legacyMediaApi = network->FAvatarLegacyFallbacks.contains(requestKey);
							const QString mediaPrefix = legacyMediaApi
								? QStringLiteral("/_matrix/media/v3/download/")
								: QStringLiteral("/_matrix/client/v1/media/download/");
							const QString path = mediaPrefix + QStringLiteral("%1/%2")
								.arg(QString::fromUtf8(QUrl::toPercentEncoding(mxc.host())))
								.arg(QString::fromUtf8(QUrl::toPercentEncoding(mediaId)));
							QNetworkRequest request(QUrl(network->constructUrl(path)));
							request.setAttribute(QNetworkRequest::Http2AllowedAttribute, network->FUseHttp2);
							request.setRawHeader("Authorization",
								QByteArray("Bearer ") + network->FAccesToken.toUtf8());
							QNetworkReply *reply = network->FNetworkAccessManager->get(request);
							connect(reply, &QNetworkReply::finished, network,
								[network, reply, key, mxcUrl, requestKey, cachePath, done]() {
									if (!network) {
										reply->deleteLater();
										done(QImage());
										return;
									}
									if (reply->error() == QNetworkReply::NoError) {
										const QByteArray data = reply->readAll();
										if (!data.isEmpty()) {
											QSaveFile cacheFile(cachePath);
											if (cacheFile.open(QIODevice::WriteOnly) &&
												cacheFile.write(data) == data.size()) {
												if (!cacheFile.commit())
													qWarning() << "Matrix avatar cache commit failed:" << cachePath;
											} else {
												qWarning() << "Matrix avatar cache write failed:" << cachePath;
											}
											network->FAvatarRetries.remove(requestKey);
											network->FAvatarLegacyFallbacks.remove(requestKey);
											network->FAvatarUnavailable.remove(requestKey);
											ImageLoadScheduler *queue = ImageLoadScheduler::instance();
											if (queue)
												queue->decodeDataAsync(data, done);
											else
												done(QImage());
										} else {
											done(QImage());
										}
									} else {
										const int status = reply->attribute(
											QNetworkRequest::HttpStatusCodeAttribute).toInt();
										bool retryScheduled = false;
										if (status >= 500 && status < 600 &&
											!network->FAvatarLegacyFallbacks.contains(requestKey)) {
											network->FAvatarLegacyFallbacks.insert(requestKey);
											retryScheduled = true;
										} else if (reply->error() == QNetworkReply::RemoteHostClosedError &&
											!network->FAvatarRetries.contains(requestKey)) {
											network->FAvatarRetries.insert(requestKey);
											network->FAvatarUnavailable.remove(requestKey);
											network->FUseHttp2 = !network->FUseHttp2;
											retryScheduled = true;
										}
										if (retryScheduled) {
											QTimer::singleShot(1000, network, [network, key, mxcUrl]() {
												if (network)
													network->requestAvatar(key, mxcUrl);
											});
										} else {
											network->FAvatarUnavailable.insert(requestKey);
										}
										done(QImage());
									}
									reply->deleteLater();
								});
						}, Qt::QueuedConnection);
				});
		}, this,
		[this, key, requestKey](const QImage &image) {
			FAvatarRequestsInFlight.remove(requestKey);
			if (image.isNull())
				return;
			FAvatarRetries.remove(requestKey);
			FAvatarLegacyFallbacks.remove(requestKey);
			FAvatarUnavailable.remove(requestKey);
			QImage avatarImage = image;
			if (avatarImage.width() > 128 || avatarImage.height() > 128)
				avatarImage = avatarImage.scaled(QSize(128, 128), Qt::KeepAspectRatio,
					Qt::SmoothTransformation);
			emit avatarImageReceived(key, avatarImage);
		});
}

void MatrixNetwork::requestImage(const MatrixTextEvent &event)
{
	if (event.messageType != QStringLiteral("m.image") &&
		event.messageType != QStringLiteral("m.file"))
		return;
	const bool isImageEvent = event.messageType == QStringLiteral("m.image");
	const QString mxcUrl = event.metadata.value(QStringLiteral("url")).toString();
	const QUrl mxc(mxcUrl);
	if (event.eventId.isEmpty() || mxc.scheme() != QStringLiteral("mxc") ||
		mxc.host().isEmpty() || mxc.path().isEmpty())
		return;
	QString profileDirectory;
	runDatabase([&](MatrixDatabase &database) {
		profileDirectory = database.profileDirectory();
	});
	const QString cacheDirectory = profileDirectory.isEmpty()
		? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/matrix-media")
		: profileDirectory + QStringLiteral("/media");
	QDir().mkpath(cacheDirectory);
	const QString cachePath = cacheDirectory + QLatin1Char('/') +
		QString::fromLatin1(QCryptographicHash::hash(mxcUrl.toUtf8(), QCryptographicHash::Sha256).toHex()) +
		QStringLiteral(".bin");
	const QString requestKey = event.eventId + QChar('\n') + mxcUrl;
	if (FImageRequestsInFlight.contains(requestKey))
		return;
	FImageRequestsInFlight.insert(requestKey);
	QPointer<MatrixNetwork> network(this);
	ImageLoadScheduler *scheduler = ImageLoadScheduler::instance();
	if (!scheduler) {
		FImageRequestsInFlight.remove(requestKey);
		return;
	}
	const QString schedulerKey = (isImageEvent ? QString() : QStringLiteral("matrix-attachment:")) +
		ImageLoadScheduler::fileKey(cachePath);
	scheduler->submit(schedulerKey,
		[network, event, mxcUrl, cachePath, isImageEvent](ImageLoadScheduler::Completion done) {
			if (!network) {
				done(QImage());
				return;
			}
			ImageLoadScheduler *queue = ImageLoadScheduler::instance();
			if (!queue) {
				done(QImage());
				return;
			}
			connect(network, &QObject::destroyed, queue, [done]() { done(QImage()); });
			queue->decodeFileAsync(cachePath,
				[network, event, mxcUrl, cachePath, isImageEvent, done](const QImage &cachedImage) {
					if (!network) {
						done(QImage());
						return;
					}
					if (!cachedImage.isNull()) {
						done(cachedImage);
						return;
					}
					if (!isImageEvent && QFileInfo(cachePath).exists() && QFileInfo(cachePath).size() > 0) {
						QImage ready(1, 1, QImage::Format_ARGB32);
						ready.fill(Qt::transparent);
						done(ready);
						return;
					}
					if (isImageEvent)
						QFile::remove(cachePath);
					QMetaObject::invokeMethod(network,
						[network, event, mxcUrl, cachePath, isImageEvent, done]() {
							if (!network) {
								done(QImage());
								return;
							}
							if (network->FAccesToken.isEmpty()) {
								done(QImage());
								return;
							}
							const QUrl mxc(mxcUrl);
							const QString path = QStringLiteral("/_matrix/client/v1/media/download/%1/%2")
								.arg(QString::fromUtf8(QUrl::toPercentEncoding(mxc.host())))
								.arg(QString::fromUtf8(QUrl::toPercentEncoding(mxc.path().mid(1))));
							QNetworkRequest request(QUrl(network->constructUrl(path)));
							request.setAttribute(QNetworkRequest::Http2AllowedAttribute, network->FUseHttp2);
							request.setRawHeader("Authorization",
								QByteArray("Bearer ") + network->FAccesToken.toUtf8());
							QNetworkReply *reply = network->FNetworkAccessManager->get(request);
							const auto finishMediaReply = [network, cachePath, isImageEvent, done](QNetworkReply *mediaReply) {
								if (!network) {
									mediaReply->deleteLater();
									done(QImage());
									return;
								}
								if (mediaReply->error() != QNetworkReply::NoError) {
									qWarning() << "Matrix image download failed:" << mediaReply->errorString()
										<< "httpStatus:"
										<< mediaReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
									mediaReply->deleteLater();
									done(QImage());
									return;
								}
								const QByteArray data = mediaReply->readAll();
								if (data.isEmpty()) {
									mediaReply->deleteLater();
									done(QImage());
									return;
								}
								QSaveFile output(cachePath);
								if (!output.open(QIODevice::WriteOnly) ||
									output.write(data) != data.size() || !output.commit())
									qWarning() << "Failed to persist Matrix room image in profile cache";
								mediaReply->deleteLater();
								ImageLoadScheduler *queue = ImageLoadScheduler::instance();
								if (!queue) {
									done(QImage());
								} else if (isImageEvent) {
									queue->decodeDataAsync(data, done);
								} else {
									queue->decodeDataAsync(data, [done](const QImage &image) {
										if (!image.isNull()) {
											done(image);
										return;
										}
										QImage ready(1, 1, QImage::Format_ARGB32);
										ready.fill(Qt::transparent);
										done(ready);
									});
								}
							};
							const auto requestLegacyMedia = [network, mxcUrl, finishMediaReply, done]() {
								if (!network) {
									done(QImage());
									return;
								}
								const QUrl legacyMxc(mxcUrl);
								const QString legacyPath = QStringLiteral("/_matrix/media/v3/download/%1/%2")
									.arg(QString::fromUtf8(QUrl::toPercentEncoding(legacyMxc.host())))
									.arg(QString::fromUtf8(QUrl::toPercentEncoding(legacyMxc.path().mid(1))));
								QNetworkRequest legacyRequest(QUrl(network->constructUrl(legacyPath)));
								legacyRequest.setAttribute(QNetworkRequest::Http2AllowedAttribute, network->FUseHttp2);
								QNetworkReply *legacyReply = network->FNetworkAccessManager->get(legacyRequest);
								connect(legacyReply, &QNetworkReply::finished, network,
									[network, legacyReply, finishMediaReply, done]() {
										if (!network) {
										legacyReply->deleteLater();
										done(QImage());
										return;
										}
										finishMediaReply(legacyReply);
									});
							};
							connect(reply, &QNetworkReply::finished, network,
								[network, reply, requestLegacyMedia, finishMediaReply, done]() {
									if (!network) {
										reply->deleteLater();
										done(QImage());
										return;
									}
									if (reply->error() != QNetworkReply::NoError) {
										const int statusCode = reply->attribute(
											QNetworkRequest::HttpStatusCodeAttribute).toInt();
										QJsonParseError parseError;
										const QJsonDocument errorDocument = QJsonDocument::fromJson(
											reply->readAll(), &parseError);
										const QString errorCode = errorDocument.object()
											.value(QStringLiteral("errcode")).toString();
										if (statusCode == 404 &&
											(errorCode == QStringLiteral("M_UNRECOGNIZED") ||
											 errorCode == QStringLiteral("M_NOT_FOUND"))) {
											reply->deleteLater();
											requestLegacyMedia();
											return;
										}
									}
									finishMediaReply(reply);
								});
						}, Qt::QueuedConnection);
				});
		}, this,
		[this, event, requestKey, cachePath](const QImage &image) {
			FImageRequestsInFlight.remove(requestKey);
			if (image.isNull())
				return;
			MatrixTextEvent imageEvent = event;
			if (imageEvent.messageType == QStringLiteral("m.image")) {
				imageEvent.metadata.insert(QStringLiteral("decoded_image"), image);
				imageEvent.metadata.insert(QStringLiteral("image_resource_url"),
					QStringLiteral("vacuum-matrix-image:/%1/%2")
						.arg(QString::fromLatin1(QUrl::toPercentEncoding(imageEvent.roomId)),
							QString::fromLatin1(QUrl::toPercentEncoding(imageEvent.eventId))));
			}
			imageEvent.metadata.insert(QStringLiteral("file_path"), cachePath);
			for (MatrixTextEvent &cached : FMessageHistory[imageEvent.roomId])
				if (cached.eventId == imageEvent.eventId)
					cached.metadata = imageEvent.metadata;
			emit messageReceived(imageEvent.toBasicMessage());
		});
}

void MatrixNetwork::requestHistoricalImages(const QString &roomId,
	const QList<MatrixTimelineEvent> &events)
{
	if (roomId.isEmpty() || events.isEmpty())
		return;

	constexpr int requestLimit = 8;
	int requested = 0;
	for (auto it = events.crbegin(); it != events.crend() && requested < requestLimit; ++it) {
		if (it->roomId != roomId || it->messageType != QStringLiteral("m.image"))
			continue;
		const QString mxcUrl = it->metadata.value(QStringLiteral("url")).toString();
		const QUrl mxc(mxcUrl);
		if (it->eventId.isEmpty() || mxc.scheme() != QStringLiteral("mxc") || mxc.host().isEmpty())
			continue;

		MatrixTextEvent event;
		event.eventId = it->eventId;
		event.roomId = it->roomId;
		event.userId = it->sender;
		event.content = it->content;
		event.timestamp = QString::number(it->originTs);
		event.eventType = it->eventType;
		event.messageType = it->messageType;
		event.attachments = it->attachments;
		for (auto metadataIt = it->metadata.cbegin(); metadataIt != it->metadata.cend(); ++metadataIt)
			event.metadata.insert(metadataIt.key(), metadataIt.value());
		event.metadata.insert(QStringLiteral("historical"), true);
		requestImage(event);
		++requested;
	}
}

void MatrixNetwork::requestRoomName(const QString &roomId)
{
	if (roomId.isEmpty() || FAccesToken.isEmpty() || FRoomNameRequests.contains(roomId))
		return;
	FRoomNameRequests.insert(roomId);
	const QString encodedRoomId = QString::fromUtf8(QUrl::toPercentEncoding(roomId));
	const auto requestFullState = [this, roomId, encodedRoomId]() {
		const QString path = QStringLiteral("/_matrix/client/v3/rooms/%1/state")
			.arg(encodedRoomId);
		QNetworkRequest request(QUrl(constructUrl(path)));
		request.setAttribute(QNetworkRequest::Http2AllowedAttribute, FUseHttp2);
		request.setRawHeader("Authorization", QByteArray("Bearer ") + FAccesToken.toUtf8());
		QNetworkReply *reply = FNetworkAccessManager->get(request);
		connect(reply, &QNetworkReply::finished, this, [this, reply, roomId]() {
			QString roomName;
			if (reply->error() == QNetworkReply::NoError) {
				const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());
				for (const QJsonValue &stateValue : document.array()) {
					const QJsonObject state = stateValue.toObject();
					if (state.value(QStringLiteral("type")).toString() == QStringLiteral("m.room.name") &&
						state.value(QStringLiteral("state_key")).toString().isEmpty()) {
						roomName = state.value(QStringLiteral("content")).toObject()
							.value(QStringLiteral("name")).toString();
						break;
					}
				}
			} else {
				qWarning() << "Matrix room-state request failed:" << reply->errorString()
					<< "httpStatus:"
					<< reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
			}
			FRoomNameRequests.remove(roomId);
			emit roomNameReceived(roomId, roomName);
			reply->deleteLater();
		});
	};
	const QString path = QStringLiteral("/_matrix/client/v3/rooms/%1/state/m.room.name")
		.arg(encodedRoomId);
	QNetworkRequest request(QUrl(constructUrl(path)));
	request.setAttribute(QNetworkRequest::Http2AllowedAttribute, FUseHttp2);
	request.setRawHeader("Authorization", QByteArray("Bearer ") + FAccesToken.toUtf8());
	QNetworkReply *reply = FNetworkAccessManager->get(request);
	connect(reply, &QNetworkReply::finished, this,
		[this, reply, roomId, requestFullState]() {
			if (reply->error() == QNetworkReply::NoError) {
				const QJsonObject content = QJsonDocument::fromJson(reply->readAll()).object();
				const QString roomName = content.value(QStringLiteral("name")).toString();
				if (!roomName.isEmpty()) {
					FRoomNameRequests.remove(roomId);
					emit roomNameReceived(roomId, roomName);
					reply->deleteLater();
					return;
				}
			} else {
				qWarning() << "Matrix room-name request failed:" << reply->errorString()
					<< "httpStatus:"
					<< reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
			}
			reply->deleteLater();
			requestFullState();
		});
}

void MatrixNetwork::requestJoinedMembers(const QString &roomId)
{
	if (roomId.isEmpty() || FAccesToken.isEmpty() || FJoinedMembersRequests.contains(roomId) ||
		FJoinedMembersLoaded.contains(roomId))
		return;
	FJoinedMembersRequests.insert(roomId);
	const QString path = QStringLiteral("/_matrix/client/v3/rooms/%1/joined_members")
		.arg(QString::fromUtf8(QUrl::toPercentEncoding(roomId)));
	QNetworkRequest request(QUrl(constructUrl(path)));
	request.setAttribute(QNetworkRequest::Http2AllowedAttribute, FUseHttp2);
	request.setRawHeader("Authorization", QByteArray("Bearer ") + FAccesToken.toUtf8());
	QNetworkReply *reply = FNetworkAccessManager->get(request);
	reply->setProperty("requestType", QStringLiteral("joined_members"));
	reply->setProperty("roomId", roomId);
}

void MatrixNetwork::emitRosterSnapshot()
{
	QList<ProtocolRoom> currentRooms;
	QStringList memberIds;
	for (auto roomIt = FRooms.begin(); roomIt != FRooms.end(); ++roomIt) {
		roomIt->isDirect = FHasDirectRoomData && FDirectRoomIds.contains(roomIt.key());
		if (roomIt->isDirect)
			for (const ProtocolRosterEntry &member : roomIt->members)
				if (member.id != FUserId) {
					roomIt->name = member.name.isEmpty() ? member.id : member.name;
					break;
				}
		for (const ProtocolRosterEntry &member : roomIt->members)
			if (!member.id.isEmpty())
				memberIds.append(member.id);
	}
	memberIds.removeDuplicates();
	QMap<QString, bool> verificationStates;
	bool verificationStatesLoaded = false;
	runDatabase([&](MatrixDatabase &database) {
		verificationStatesLoaded = database.userVerificationStates(memberIds, verificationStates);
	});
	for (auto roomIt = FRooms.begin(); roomIt != FRooms.end(); ++roomIt) {
		for (ProtocolRosterEntry &member : roomIt->members) {
			member.hasVerificationState = verificationStatesLoaded;
			member.isVerified = verificationStates.value(member.id, false);
		}
		currentRooms.append(roomIt.value());
	}
	emit rosterChanged(currentRooms);
}

void MatrixNetwork::refreshRosterSnapshot()
{
	emitRosterSnapshot();
}

void MatrixNetwork::setKnownRoomTypes(const QVariantMap &roomTypes)
{
	for (auto it = roomTypes.constBegin(); it != roomTypes.constEnd(); ++it) {
		const QString roomType = it.value().toString();
		if (!it.key().isEmpty() && !roomType.isEmpty())
			FKnownRoomTypes.insert(it.key(), roomType);
	}
}

void MatrixNetwork::requestDisplayName(const QString &userId)
{
	if (userId.isEmpty() || FAccesToken.isEmpty() || FDisplayNameRequests.contains(userId))
		return;
	FDisplayNameRequests.insert(userId);
	const QString path = QStringLiteral("/_matrix/client/v3/profile/%1/displayname")
		.arg(QString::fromUtf8(QUrl::toPercentEncoding(userId)));
	QNetworkRequest request(QUrl(constructUrl(path)));
	request.setAttribute(QNetworkRequest::Http2AllowedAttribute, FUseHttp2);
	request.setRawHeader("Authorization", QByteArray("Bearer ") + FAccesToken.toUtf8());
	QNetworkReply *reply = FNetworkAccessManager->get(request);
	reply->setProperty("requestType", QStringLiteral("display_name"));
	reply->setProperty("displayNameUserId", userId);
}

void MatrixNetwork::uploadFileAndSend(const QString &roomId, const QString &filePath,
	const QString &mimeType, const QString &messageType, const QString &body, const QString &txnId)
{
	if (roomId.isEmpty() || filePath.isEmpty() || FAccesToken.isEmpty())
		return;
	QFile file(filePath);
	if (!file.open(QIODevice::ReadOnly)) {
		emit sendError(QStringLiteral("Datei konnte nicht geöffnet werden: %1").arg(filePath));
		return;
	}
	const QByteArray data = file.readAll();
	const QFileInfo info(filePath);
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("filename"), info.fileName());
	QUrl url(constructUrl(QStringLiteral("/_matrix/media/v3/upload")));
	url.setQuery(query);
	QNetworkRequest request(url);
	request.setAttribute(QNetworkRequest::Http2AllowedAttribute, FUseHttp2);
	request.setRawHeader("Authorization", QByteArray("Bearer ") + FAccesToken.toUtf8());
	request.setHeader(QNetworkRequest::ContentTypeHeader, mimeType.isEmpty()
		? QStringLiteral("application/octet-stream") : mimeType);
	QNetworkReply *reply = FNetworkAccessManager->post(request, data);
	reply->setProperty("requestType", QStringLiteral("media_upload"));
	reply->setProperty("conversationId", roomId);
	reply->setProperty("fileMessageType", messageType);
	reply->setProperty("fileBody", body.isEmpty() ? info.fileName() : body);
	reply->setProperty("fileMimeType", mimeType);
	reply->setProperty("transactionId", txnId);
}

void MatrixNetwork::sendMessageEvent(const QString &roomId, const QJsonObject &content, const QString &txnId)
{
	sendRoomEvent(roomId, QStringLiteral("m.room.message"), content, txnId);
}

void MatrixNetwork::sendRoomEvent(const QString &roomId, const QString &eventType,
	const QJsonObject &content, const QString &txnId)
{
	if (!roomId.startsWith(QLatin1Char('!')) || FAccesToken.isEmpty() ||
		(eventType != QStringLiteral("m.room.message") && eventType != QStringLiteral("m.reaction"))) {
		emit sendError(QStringLiteral("Cannot send Matrix message without a joined room and access token"));
		return;
	}
	QString txId = txnId.isEmpty() ? generateTransactionId() : txnId;
	QJsonObject wireContent = content;
	QString sendType = eventType;
	if (eventType == QStringLiteral("m.room.message") && FRooms.value(roomId).isEncrypted) {
		QString sessionId;
		QByteArray sessionKey;
		QByteArray storedPickle;
		bool haveSession = false;
		runDatabase([&](MatrixDatabase &database) {
			haveSession = database.loadOutboundMegolmSession(roomId, sessionId, storedPickle);
		});
		if (haveSession)
			haveSession = FOlmCrypto.exportOutboundMegolmSessionWithPickle(roomId, sessionId,
				storedPickle, sessionId, sessionKey);
		if (haveSession && sessionId == QString(43, QLatin1Char('A'))) {
			qWarning() << "[Matrix-E2EE] discarding placeholder outbound Megolm session"
				<< "room:" << roomId;
			haveSession = false;
			sessionId.clear();
			sessionKey.clear();
		}
		if (haveSession) {
			QList<QPair<QString, QString>> sharedDevices;
			runDatabase([&](MatrixDatabase &database) {
				sharedDevices = database.megolmKeyShareDevices(sessionId);
			});
			QSet<QString> currentRecipients;
			const auto roomIt = FRooms.constFind(roomId);
			if (roomIt != FRooms.constEnd()) {
				for (const ProtocolRosterEntry &member : roomIt->members) {
					if (!member.isValid)
						continue;
					QMap<QString, QByteArray> devices;
					runDatabase([&](MatrixDatabase &database) {
						devices = database.loadDeviceKeysForUser(member.id);
					});
					for (auto deviceIt = devices.constBegin(); deviceIt != devices.constEnd(); ++deviceIt)
						if (!(member.id == FUserId && deviceIt.key() == FDeviceId))
							currentRecipients.insert(member.id + QLatin1Char('\n') + deviceIt.key());
				}
			}
			for (const auto &shared : sharedDevices) {
				if (!currentRecipients.contains(shared.first + QLatin1Char('\n') + shared.second)) {
					haveSession = false;
					break;
				}
			}
		}
		if (haveSession) {
			qint64 createdAt = 0;
			runDatabase([&](MatrixDatabase &database) {
				createdAt = database.outboundMegolmSessionCreatedAt(roomId);
			});
			const bool messageLimitReached = FOlmCrypto.outboundMegolmMessageIndexForLoadedSession(roomId) >= 100;
			const bool ageLimitReached = createdAt > 0 &&
				QDateTime::currentSecsSinceEpoch() - createdAt >= 7 * 24 * 60 * 60;
			if (messageLimitReached || ageLimitReached)
				haveSession = false;
		}
		if (!haveSession) {
			QByteArray sessionPickle;
			if (!FOlmCrypto.createOutboundMegolmSessionData(roomId, sessionId, sessionKey,
				sessionPickle)) {
				emit sendError(QStringLiteral("Encrypted Matrix room has no outbound Megolm session"));
				return;
			}
			bool sessionSaved = false;
			runDatabase([&](MatrixDatabase &database) {
				sessionSaved = database.saveOutboundMegolmSession(roomId, sessionId,
					sessionPickle, QDateTime::currentSecsSinceEpoch());
			});
			if (!sessionSaved) {
			emit sendError(QStringLiteral("Encrypted Matrix room has no outbound Megolm session"));
			return;
			}
		}
		bool inboundSessionExists = false;
		runDatabase([&](MatrixDatabase &database) {
			QString inboundRoomId;
			QString inboundSenderKey;
			QByteArray inboundPickle;
			inboundSessionExists = database.loadMegolmSession(sessionId, inboundRoomId,
				inboundSenderKey, inboundPickle);
		});
		if (!inboundSessionExists) {
			const QJsonObject identity = QJsonDocument::fromJson(FOlmCrypto.identityKeysJson()).object();
			const QString localSenderKey = identity.value(QStringLiteral("curve25519")).toString();
			bool inboundImported = false;
			if (!localSenderKey.isEmpty())
			    runDatabase([&](MatrixDatabase &database) {
			        inboundImported = FOlmCrypto.importMegolmSession(database, sessionId, roomId,
			            localSenderKey, sessionKey);
			    });
			else
			    qWarning() << "[Matrix-E2EE] local Curve25519 identity unavailable for outbound room session";
			if (!inboundImported) {
				emit sendError(localSenderKey.isEmpty()
					? QStringLiteral("Encrypted Matrix room local identity unavailable")
					: QStringLiteral("Encrypted Matrix room local Megolm import/save failed"));
				return;
			}
		}
		if (!distributeOutboundRoomKey(roomId, sessionId, sessionKey)) {
			QJsonArray &pending = FPendingEncryptedMessages[roomId];
			pending.append(QJsonObject{{QStringLiteral("room_id"), roomId},
				{QStringLiteral("event_type"), eventType}, {QStringLiteral("content"), content},
				{QStringLiteral("transaction_id"), txId}});
			const auto roomIt = FRooms.constFind(roomId);
			if (roomIt != FRooms.constEnd())
				for (const ProtocolRosterEntry &member : roomIt->members)
					if (member.isValid && member.id != FUserId)
						queryDeviceKeys(member.id);
			emit sendError(QStringLiteral("Matrix room key could not be distributed to all devices"));
			return;
		}
		QByteArray encryptedPickle;
		const QJsonObject megolmEvent{
			{QStringLiteral("room_id"), roomId},
			{QStringLiteral("type"), eventType},
			{QStringLiteral("content"), content}};
		QByteArray ciphertext = FOlmCrypto.encryptMegolmData(roomId,
			QJsonDocument(megolmEvent).toJson(QJsonDocument::Compact), sessionId, encryptedPickle);
		if (ciphertext.isEmpty()) {
			emit sendError(QStringLiteral("Encrypted Matrix message could not be encrypted"));
			return;
		}
		if (!distributeOutboundRoomKey(roomId, sessionId, sessionKey)) {
			emit sendError(QStringLiteral("Matrix room key does not match encrypted session"));
			return;
		}
		bool encryptedSessionSaved = false;
		runDatabase([&](MatrixDatabase &database) {
			encryptedSessionSaved = database.saveOutboundMegolmSession(roomId, sessionId,
				encryptedPickle);
		});
		if (!encryptedSessionSaved) {
			emit sendError(QStringLiteral("Encrypted Matrix session persistence failed"));
			return;
		}
		QJsonParseError identityError;
		const QJsonObject identity = QJsonDocument::fromJson(FOlmCrypto.identityKeysJson(),
			&identityError).object();
		const QString senderKey = identity.value(QStringLiteral("curve25519")).toString();
		if (ciphertext.isEmpty() || senderKey.isEmpty()) {
			emit sendError(QStringLiteral("Encrypted Matrix message could not be encrypted"));
			return;
		}
		wireContent = QJsonObject{{QStringLiteral("algorithm"), QStringLiteral("m.megolm.v1.aes-sha2")},
			{QStringLiteral("ciphertext"), QString::fromUtf8(ciphertext)},
			{QStringLiteral("device_id"), FDeviceId},
			{QStringLiteral("sender_key"), senderKey},
			{QStringLiteral("session_id"), sessionId}};
		sendType = QStringLiteral("m.room.encrypted");
	}
	MatrixTextEvent pending;
	pending.roomId = roomId;
	pending.eventId = txId;
	pending.userId = FUserId;
	pending.timestamp = QString::number(QDateTime::currentMSecsSinceEpoch());
	pending.eventType = eventType;
	pending.messageType = eventType == QStringLiteral("m.room.message")
		? content.value(QStringLiteral("msgtype")).toString() : QString();
	pending.content = content.value(QStringLiteral("body")).toString();
	pending.metadata.insert(QStringLiteral("txn_id"), txId);
	pending.metadata.insert(QStringLiteral("event_type"), eventType);
	const QJsonObject relation = content.value(QStringLiteral("m.relates_to")).toObject();
	if (!relation.isEmpty()) {
		pending.metadata.insert(QStringLiteral("relation_type"), relation.value(QStringLiteral("rel_type")).toString());
		pending.metadata.insert(QStringLiteral("related_event_id"), relation.value(QStringLiteral("event_id")).toString());
		pending.metadata.insert(QStringLiteral("reaction_key"), relation.value(QStringLiteral("key")).toString());
	}
	mergeMessageEvent(pending);
	MatrixTimelineEvent pendingStored;
	pendingStored.roomId = pending.roomId;
	pendingStored.eventId = pending.eventId;
	pendingStored.eventType = pending.eventType;
	pendingStored.sender = pending.userId;
	pendingStored.originTs = pending.timestamp.toLongLong();
	pendingStored.messageType = pending.messageType;
	pendingStored.content = pending.content;
	for (auto metadataIt = pending.metadata.constBegin(); metadataIt != pending.metadata.constEnd(); ++metadataIt)
		pendingStored.metadata.insert(metadataIt.key(), metadataIt.value());
	runDatabase([&](MatrixDatabase &database) {
		database.appendTimelineEvents(roomId, {pendingStored});
	});
	
	const QString sendPath = QStringLiteral("/_matrix/client/v3/rooms/") +
		QString::fromUtf8(QUrl::toPercentEncoding(roomId)) +
		QStringLiteral("/send/") + sendType + QLatin1Char('/') + txId;
	
	QUrl sendUrl(constructUrl(sendPath));
	QNetworkRequest request(sendUrl);
	request.setAttribute(QNetworkRequest::Http2AllowedAttribute, FUseHttp2);
	request.setRawHeader("Authorization", QByteArray("Bearer ") + FAccesToken.toUtf8());
	request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	
	QJsonDocument doc(wireContent);
	const bool messageEvent = eventType == QStringLiteral("m.room.message");
	if (messageEvent) {
		runDatabase([&](MatrixDatabase &database) {
			database.saveOutboxMessage(roomId, txId,
				content.value(QStringLiteral("body")).toString(), QStringLiteral("queued"));
		});
		emit messageDeliveryChanged(roomId, txId, QStringLiteral("queued"), QString());
	}
	
	QNetworkReply *reply = FNetworkAccessManager->put(request, doc.toJson(QJsonDocument::Compact));
	reply->setProperty("requestType", "send");
	reply->setProperty("conversationId", roomId);
	reply->setProperty("transactionId", txId);
	reply->setProperty("body", content.value(QStringLiteral("body")).toString());
	FSendInFlight = true;
	if (messageEvent) {
		runDatabase([&](MatrixDatabase &database) {
			database.saveOutboxMessage(roomId, txId,
				content.value(QStringLiteral("body")).toString(), QStringLiteral("sending"));
		});
		emit messageDeliveryChanged(roomId, txId, QStringLiteral("sending"), QString());
	}
	reply->setProperty("messageEvent", messageEvent);
}

bool MatrixNetwork::sendVerificationEvent(const QString &eventType, const QString &transactionId,
	const QString &userId, const QString &deviceId, const QJsonObject &content)
{
	if (FAccesToken.isEmpty() || eventType.isEmpty() || transactionId.isEmpty() ||
		FDeviceId.isEmpty() || userId.isEmpty() || deviceId.isEmpty() ||
		!eventType.startsWith(QStringLiteral("m.key.verification.")))
		return false;
	QJsonObject devices;
	devices.insert(deviceId, content);
	QJsonObject users;
	users.insert(userId, devices);
	QJsonObject body;
	body.insert(QStringLiteral("messages"), users);
	const QString path = QStringLiteral("/_matrix/client/v3/sendToDevice/%1/%2")
		.arg(QString::fromUtf8(QUrl::toPercentEncoding(eventType)), generateTransactionId());
	QNetworkRequest request(QUrl(constructUrl(path)));
	request.setAttribute(QNetworkRequest::Http2AllowedAttribute, FUseHttp2);
	request.setRawHeader("Authorization", QByteArray("Bearer ") + FAccesToken.toUtf8());
	request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
	QNetworkReply *reply = FNetworkAccessManager->put(request,
		QJsonDocument(body).toJson(QJsonDocument::Compact));
	const QString verificationCode = content.value(QStringLiteral("code")).toString();
	connect(reply, &QNetworkReply::finished, this,
		[reply, eventType, userId, deviceId, transactionId, verificationCode]() {
		qWarning() << "[Matrix-E2EE] direct verification reply:"
			<< eventType << "target:" << userId << deviceId
			<< "transaction:" << transactionId
			<< "status:" << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
			<< "qtError:" << reply->error()
			<< "code:" << verificationCode
			<< "error:" << (reply->error() == QNetworkReply::NoError
				? QStringLiteral("none") : reply->errorString());
	});
	reply->setProperty("requestType", QStringLiteral("to_device"));
	reply->setProperty("e2eeEventType", eventType);
	reply->setProperty("e2eeTargetUser", userId);
	reply->setProperty("e2eeTargetDevice", deviceId);
	qWarning() << "[Matrix-E2EE] sending verification event:" << eventType
		<< "from_device:" << FDeviceId << "target:" << userId << deviceId
		<< "transaction:" << transactionId;
	return true;
}

bool MatrixNetwork::requestSasVerification(const QString &transactionId, const QString &userId,
	const QString &deviceId)
{
	if (transactionId.isEmpty() || userId.isEmpty() || deviceId.isEmpty() ||
		FSasStates.contains(transactionId))
		return false;
	FDeviceKeyQueries.remove(userId);
	FDeviceKeyQueries.insert(userId);
	// Cross-signing master keys may be signed by any device of the user.
	// Query all devices so the signer is present in the same response.
	queryDeviceKeys(userId);
	const QJsonObject content{{QStringLiteral("from_device"), FDeviceId},
		{QStringLiteral("methods"), QJsonArray{QStringLiteral("m.sas.v1")}},
		{QStringLiteral("timestamp"), QDateTime::currentMSecsSinceEpoch()},
		{QStringLiteral("transaction_id"), transactionId}};
	if (!sendVerificationEvent(QStringLiteral("m.key.verification.request"), transactionId,
		userId, deviceId, content))
		return false;
	FSasStates.insert(transactionId, QStringLiteral("requested"));
	runDatabase([&](MatrixDatabase &database) {
		database.saveVerificationState(transactionId, userId, deviceId,
			QStringLiteral("requested"), QJsonDocument(content).toJson(QJsonDocument::Compact));
	});
	emit verificationStateChanged(transactionId, QStringLiteral("requested"));
	return true;
}

bool MatrixNetwork::cancelSasVerification(const QString &transactionId, const QString &userId,
	const QString &deviceId)
{
	if (transactionId.isEmpty() || userId.isEmpty() || deviceId.isEmpty())
		return false;
	const QJsonObject content{{QStringLiteral("transaction_id"), transactionId},
		{QStringLiteral("from_device"), FDeviceId},
		{QStringLiteral("code"), QStringLiteral("m.user")}};
	if (!sendVerificationEvent(QStringLiteral("m.key.verification.cancel"), transactionId,
		userId, deviceId, content))
		return false;
	FSasStates.insert(transactionId, QStringLiteral("cancelled"));
	runDatabase([&](MatrixDatabase &database) {
		database.saveVerificationState(transactionId, userId, deviceId,
			QStringLiteral("cancelled"), QJsonDocument(content).toJson(QJsonDocument::Compact));
	});
	emit verificationStateChanged(transactionId, QStringLiteral("cancelled"));
	return true;
}

bool MatrixNetwork::acceptSasVerification(const QString &transactionId, const QString &userId,
	const QString &deviceId)
{
	if (FSasStates.value(transactionId) != QStringLiteral("requested"))
		return false;
	const QJsonObject content{{QStringLiteral("from_device"), FDeviceId},
		{QStringLiteral("methods"), QJsonArray{QStringLiteral("m.sas.v1")}},
		{QStringLiteral("transaction_id"), transactionId}};
	if (!sendVerificationEvent(QStringLiteral("m.key.verification.ready"), transactionId,
		userId, deviceId, content))
		return false;
	FSasStates.insert(transactionId, QStringLiteral("ready"));
	runDatabase([&](MatrixDatabase &database) {
		database.saveVerificationState(transactionId, userId, deviceId,
			QStringLiteral("ready"), QJsonDocument(content).toJson(QJsonDocument::Compact));
	});
	emit verificationStateChanged(transactionId, QStringLiteral("ready"));
	return true;
}

bool MatrixNetwork::startSasVerification(const QString &transactionId, const QString &userId,
	const QString &deviceId)
{
	if (FSasStates.value(transactionId) != QStringLiteral("ready"))
		return false;
	QByteArray publicKey;
	if (!FOlmCrypto.createSas(transactionId, publicKey))
		return false;
	FOlmCrypto.setSasMacMethod(transactionId, QStringLiteral("hkdf-hmac-sha256"));
	FSasPublicKeys.insert(transactionId, publicKey);
	FSasInitiatorUsers.insert(transactionId, FUserId);
	FSasInitiatorDevices.insert(transactionId, FDeviceId);
	FSasPeerDevices.insert(transactionId, deviceId);
	const QJsonObject content{{QStringLiteral("from_device"), FDeviceId},
		{QStringLiteral("hashes"), QJsonArray{QStringLiteral("sha256")}},
		{QStringLiteral("key_agreement_protocols"), QJsonArray{QStringLiteral("curve25519-hkdf-sha256")}},
		{QStringLiteral("message_authentication_codes"), QJsonArray{
			QStringLiteral("hkdf-hmac-sha256.v2"), QStringLiteral("hkdf-hmac-sha256")}},
		{QStringLiteral("method"), QStringLiteral("m.sas.v1")},
		{QStringLiteral("short_authentication_string"), QJsonArray{QStringLiteral("decimal"), QStringLiteral("emoji")}},
		{QStringLiteral("transaction_id"), transactionId}};
	if (!sendVerificationEvent(QStringLiteral("m.key.verification.start"), transactionId,
		userId, deviceId, content))
		return false;
	FSasStates.insert(transactionId, QStringLiteral("started"));
	FSasStartContents.insert(transactionId, content);
	runDatabase([&](MatrixDatabase &database) {
		database.saveVerificationState(transactionId, userId, deviceId,
			QStringLiteral("started"), QJsonDocument(content).toJson(QJsonDocument::Compact));
	});
	emit verificationStateChanged(transactionId, QStringLiteral("started"));
	return true;
}

bool MatrixNetwork::confirmSasVerification(const QString &transactionId, const QString &userId,
	const QString &deviceId)
{
	qWarning() << "[Matrix-E2EE] confirming SAS:" << transactionId
		<< "state:" << FSasStates.value(transactionId)
		<< "user:" << userId << "device:" << deviceId;
	if ((FSasStates.value(transactionId) != QStringLiteral("key_received") &&
		 FSasStates.value(transactionId) != QStringLiteral("mac_verified")) ||
		userId.isEmpty() || deviceId.isEmpty()) {
		qWarning() << "[Matrix-E2EE] SAS confirm rejected:" << transactionId
			<< "state:" << FSasStates.value(transactionId)
			<< "userEmpty:" << userId.isEmpty() << "deviceEmpty:" << deviceId.isEmpty();
		return false;
	}
	const QJsonObject identityKeys = QJsonDocument::fromJson(FOlmCrypto.identityKeysJson()).object();
	if (identityKeys.isEmpty()) {
		qWarning() << "[Matrix-E2EE] SAS confirm rejected: identity keys unavailable:" << transactionId;
		return false;
	}
	const QByteArray macInfo = QStringLiteral("MATRIX_KEY_VERIFICATION_MAC%1%2%3%4%5")
		.arg(FUserId, FDeviceId, userId, deviceId, transactionId).toUtf8();
	QJsonObject macs;
	QStringList keyIds;
	const QString identityKey = identityKeys.value(QStringLiteral("ed25519")).toString();
	const QString identityKeyId = QStringLiteral("ed25519:%1").arg(FDeviceId);
	if (identityKey.isEmpty()) {
		qWarning() << "[Matrix-E2EE] SAS confirm rejected: ed25519 identity key unavailable:" << transactionId;
		return false;
	}
	QMap<QString, QString> keysToMac;
	keysToMac.insert(identityKeyId, identityKey);
	for (auto keyIt = keysToMac.constBegin(); keyIt != keysToMac.constEnd(); ++keyIt) {
		const QByteArray mac = FOlmCrypto.calculateSasMac(transactionId,
			keyIt.value().toUtf8(), macInfo + keyIt.key().toUtf8());
		if (mac.isEmpty())
			return false;
		keyIds.append(keyIt.key());
		macs.insert(keyIt.key(), QString::fromLatin1(mac));
	}
	std::sort(keyIds.begin(), keyIds.end());
	qWarning() << "[Matrix-E2EE] outgoing SAS MAC inputs:" << transactionId
		<< "keyIds:" << keyIds
		<< "crossSigningMasterIncluded:" << false
		<< "macInfoHash:" << QCryptographicHash::hash(macInfo,
			QCryptographicHash::Sha256).toHex();
	if (keyIds.isEmpty()) {
		qWarning() << "[Matrix-E2EE] SAS confirm rejected: no ed25519 identity key:" << transactionId;
		return false;
	}
	const QByteArray keysMac = FOlmCrypto.calculateSasMac(transactionId,
		keyIds.join(',').toUtf8(), macInfo + QByteArrayLiteral("KEY_IDS"));
	if (keysMac.isEmpty())
		return false;
	const QJsonObject content{{QStringLiteral("transaction_id"), transactionId},
		{QStringLiteral("from_device"), FDeviceId},
		{QStringLiteral("mac"), macs},
		{QStringLiteral("keys"), QString::fromLatin1(keysMac)}};
	if (!sendVerificationEvent(QStringLiteral("m.key.verification.mac"), transactionId,
		userId, deviceId, content))
		return false;
	qWarning() << "[Matrix-E2EE] SAS MAC sent after UI confirmation:" << transactionId
		<< "keyCount:" << keyIds.size()
		<< "macMethod:" << (FOlmCrypto.sasMacMethod(transactionId));
	// The peer must receive and validate this MAC before it receives done.  These
	// are independent asynchronous /sendToDevice requests, so sending both here
	// can reorder them on the homeserver.  Wait for the peer's done instead.
	const QString nextSasState = QStringLiteral("mac_sent");
	FSasStates.insert(transactionId, nextSasState);
	runDatabase([&](MatrixDatabase &database) {
		database.saveVerificationState(transactionId, userId, deviceId, nextSasState,
			QJsonDocument(content).toJson(QJsonDocument::Compact));
	});
	emit verificationStateChanged(transactionId, nextSasState);
	return true;
}

bool MatrixNetwork::markRoomRead(const QString &roomId, const QString &eventId)
{
	if (roomId.isEmpty() || eventId.isEmpty() || FAccesToken.isEmpty())
		return false;
	QNetworkRequest request(QUrl(constructUrl(QStringLiteral("/rooms/%1/read_markers").arg(roomId))));
	request.setAttribute(QNetworkRequest::Http2AllowedAttribute, FUseHttp2);
	request.setRawHeader("Authorization", QByteArray("Bearer ") + FAccesToken.toUtf8());
	request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
	QJsonObject payload;
	payload.insert(QStringLiteral("m.fully_read"), eventId);
	payload.insert(QStringLiteral("m.read"), eventId);
	QNetworkReply *reply = FNetworkAccessManager->put(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
	connect(reply, &QNetworkReply::finished, this, [this, reply, roomId, eventId]() {
		if (reply->error() == QNetworkReply::NoError)
			runDatabase([&](MatrixDatabase &database) {
				database.setReadMarkers(roomId, eventId, eventId);
			});
		reply->deleteLater();
	});
	return true;
}

void MatrixNetwork::onReplyFinished(QNetworkReply *reply)
{
	if (!reply) {
		return;
	}
	
	const QString type = reply->property("requestType").toString();
	
	if (type == "login") {
		onLoginFinished(reply);
	} else if (type == "sync") {
		onSyncFinished(reply);
	} else if (type == "send") {
		onSendFinished(reply);
	} else if (type == "filter_create") {
		const bool roomsOnly = reply->property("roomsOnly").toBool();
		if (reply->error() == QNetworkReply::NoError) {
			const QString filterId = QJsonDocument::fromJson(reply->readAll()).object()
				.value(QStringLiteral("filter_id")).toString();
			bool filterSaved = false;
			if (!filterId.isEmpty())
				runDatabase([&](MatrixDatabase &database) {
					filterSaved = database.saveSyncFilter(filterId);
				});
			if (!filterId.isEmpty() && filterSaved) {
				FSyncFilterId = filterId;
				sync(roomsOnly);
				return;
			}
		}
		qWarning() << "Matrix server-side filter creation failed; using inline filter"
			<< reply->errorString();
		sync(roomsOnly);
	} else if (type == "room_membership") {
		const QString roomId = reply->property("roomId").toString();
		const QString action = reply->property("membershipAction").toString();
		if (reply->error() != QNetworkReply::NoError) {
			emit syncError(QStringLiteral("Matrix room %1 failed: %2")
				.arg(action, reply->errorString()));
		} else if (FRooms.contains(roomId)) {
			ProtocolRoom &room = FRooms[roomId];
			room.membership = action;
			room.isJoined = action == QStringLiteral("join");
			room.isAvailable = room.isJoined;
			bool roomSaved = false;
			runDatabase([&](MatrixDatabase &database) {
				roomSaved = database.saveRoomState(roomId, room.name, room.subject,
					room.avatarUrl, action, room.isDirect, room.isEncrypted,
					QString(), FRoomPrevBatch.value(roomId));
		});
			if (!roomSaved)
				qWarning() << "Failed to persist Matrix membership action" << roomId << action;
			emitRosterSnapshot();
			sync();
		}
	} else if (type == "joined_members") {
		const QString roomId = reply->property("roomId").toString();
		FJoinedMembersRequests.remove(roomId);
		if (reply->error() == QNetworkReply::NoError) {
			const QJsonObject joined = QJsonDocument::fromJson(reply->readAll()).object()
				.value(QStringLiteral("joined")).toObject();
			if (FRooms.contains(roomId)) {
				ProtocolRoom &room = FRooms[roomId];
				room.members.clear();
				for (auto memberIt = joined.constBegin(); memberIt != joined.constEnd(); ++memberIt) {
					const QJsonObject memberObject = memberIt.value().toObject();
					ProtocolRosterEntry member;
					member.id = memberIt.key();
					member.name = memberObject.value(QStringLiteral("display_name")).toString();
					member.avatarUrl = memberObject.value(QStringLiteral("avatar_url")).toString();
					member.presence = QStringLiteral("unknown");
					member.isValid = true;
					room.members.append(member);
					bool memberSaved = false;
					runDatabase([&](MatrixDatabase &database) {
						memberSaved = database.saveRoomMember(roomId, member.id, QStringLiteral("join"),
							member.name, member.avatarUrl, QString());
					});
					if (!memberSaved)
						qWarning() << "Failed to persist joined Matrix member" << roomId << member.id;
				}
				if (room.memberCount < 0 || room.memberCount != room.members.size())
					room.memberCount = room.members.size();
				FJoinedMembersLoaded.insert(roomId);
				emitRosterSnapshot();
			}
		} else {
			qWarning() << "Matrix joined-members request failed:" << reply->errorString()
				<< "httpStatus:" << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		}
		reply->deleteLater();
	} else if (type == "public_rooms") {
		MatrixPublicRooms::Result result;
		if (reply->error() != QNetworkReply::NoError) {
			const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
			result.error = QStringLiteral("Matrix public-room search failed: %1 (HTTP %2)")
				.arg(reply->errorString()).arg(status);
		} else {
			result = MatrixPublicRooms::parsePublicRoomsResponse(reply->readAll());
		}
		emit publicRoomsReceived(result);
		reply->deleteLater();
	} else if (type == "create_direct_room") {
		const QString userId = reply->property("directUserId").toString();
		QString roomId;
		QString error;
		if (reply->error() != QNetworkReply::NoError) {
			error = QStringLiteral("Matrix could not create the direct room: %1").arg(reply->errorString());
		} else {
			roomId = MatrixDirectRoom::parseCreatedRoomId(reply->readAll(), error);
		}
		if (!roomId.isEmpty()) {
			const QString accountPath = QStringLiteral("/_matrix/client/v3/user/%1/account_data/m.direct")
				.arg(QString::fromUtf8(QUrl::toPercentEncoding(FUserId)));
			QNetworkRequest request(QUrl(constructUrl(accountPath)));
			request.setAttribute(QNetworkRequest::Http2AllowedAttribute, FUseHttp2);
			request.setRawHeader("Authorization", QByteArray("Bearer ") + FAccesToken.toUtf8());
			QNetworkReply *accountReply = FNetworkAccessManager->get(request);
			accountReply->setProperty("requestType", QStringLiteral("direct_room_account_data_get"));
			accountReply->setProperty("directUserId", userId);
			accountReply->setProperty("directRoomId", roomId);
		} else {
			emit directRoomCreated(userId, QString(), error);
		}
		reply->deleteLater();
	} else if (type == "direct_room_account_data_get") {
		const QString userId = reply->property("directUserId").toString();
		const QString roomId = reply->property("directRoomId").toString();
		QJsonObject mapping;
		const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (reply->error() == QNetworkReply::NoError) {
			QJsonParseError parseError;
			const QJsonDocument document = QJsonDocument::fromJson(reply->readAll(), &parseError);
			if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
				emit directRoomCreated(userId, roomId,
					QStringLiteral("Room was created, but m.direct account data could not be parsed"));
				reply->deleteLater();
				return;
			}
			mapping = document.object();
		} else if (status != 404) {
			emit directRoomCreated(userId, roomId,
				QStringLiteral("Room was created, but existing m.direct data could not be read: %1")
					.arg(reply->errorString()));
			reply->deleteLater();
			return;
		}
		const QJsonObject updatedMapping = MatrixDirectRoom::addRoomToDirectMapping(mapping, userId, roomId);
		const QString accountPath = QStringLiteral("/_matrix/client/v3/user/%1/account_data/m.direct")
			.arg(QString::fromUtf8(QUrl::toPercentEncoding(FUserId)));
		QNetworkRequest request(QUrl(constructUrl(accountPath)));
		request.setAttribute(QNetworkRequest::Http2AllowedAttribute, FUseHttp2);
		request.setRawHeader("Authorization", QByteArray("Bearer ") + FAccesToken.toUtf8());
		request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
		QNetworkReply *accountReply = FNetworkAccessManager->put(request,
			QJsonDocument(updatedMapping).toJson(QJsonDocument::Compact));
		accountReply->setProperty("requestType", QStringLiteral("direct_room_account_data_put"));
		accountReply->setProperty("directUserId", userId);
		accountReply->setProperty("directRoomId", roomId);
		reply->deleteLater();
	} else if (type == "direct_room_account_data_put") {
		const QString userId = reply->property("directUserId").toString();
		const QString roomId = reply->property("directRoomId").toString();
		if (reply->error() != QNetworkReply::NoError) {
			emit directRoomCreated(userId, roomId,
				QStringLiteral("Room was created, but m.direct account data could not be saved: %1")
					.arg(reply->errorString()));
		} else {
			FDirectRoomIds.insert(roomId);
			FHasDirectRoomData = true;
			bool directRoomsSaved = false;
			runDatabase([&](MatrixDatabase &database) {
				directRoomsSaved = database.saveDirectRooms(FDirectRoomIds);
			});
			if (FRooms.contains(roomId))
				FRooms[roomId].isDirect = true;
			if (!directRoomsSaved)
				qWarning() << "Failed to persist newly created Matrix direct-room mapping" << roomId;
			emitRosterSnapshot();
			emit directRoomCreated(userId, roomId, directRoomsSaved ? QString() :
				QStringLiteral("Room created, but the local direct-room cache could not be saved"));
			if (!FSyncInFlight)
				sync(true);
		}
		reply->deleteLater();
	} else if (type == "media_upload") {
		if (reply->error() != QNetworkReply::NoError) {
			qWarning() << "Matrix media upload failed:" << reply->errorString()
				<< "httpStatus:" << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
			emit sendError(QStringLiteral("Matrix-Datei-Upload fehlgeschlagen"));
		} else {
			const QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
			const QString contentUri = response.value(QStringLiteral("content_uri")).toString();
			if (contentUri.isEmpty()) {
				emit sendError(QStringLiteral("Matrix-Server lieferte keine Medien-URL"));
			} else {
				QJsonObject content;
				content.insert(QStringLiteral("msgtype"), reply->property("fileMessageType").toString());
				content.insert(QStringLiteral("body"), reply->property("fileBody").toString());
				content.insert(QStringLiteral("url"), contentUri);
				QJsonObject info;
				info.insert(QStringLiteral("mimetype"), reply->property("fileMimeType").toString());
				content.insert(QStringLiteral("info"), info);
				sendMessageEvent(reply->property("conversationId").toString(), content,
					reply->property("transactionId").toString());
			}
		}
	} else if (type == "display_name") {
		const QString userId = reply->property("displayNameUserId").toString();
		FDisplayNameRequests.remove(userId);
		if (reply->error() == QNetworkReply::NoError) {
			const QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
			const QString displayName = response.value(QStringLiteral("displayname")).toString();
			if (!displayName.isEmpty())
				emit displayNameReceived(userId, displayName);
		}
	} else if (type == "keys_upload") {
		FKeysUploadInFlight = false;
		const QByteArray responseBody = reply->readAll();
		if (reply->error() != QNetworkReply::NoError) {
			qWarning() << "Matrix Olm key upload failed:" << reply->errorString()
				<< "server response:" << responseBody;
			if (!FKeysUploadCollisionRetried && responseBody.contains("already exists")) {
				FKeysUploadCollisionRetried = true;
				// Nheko marks OTKs as published only after a successful upload.
				// A collision means the server rejected this request; marking the
				// local batch here would make the account state diverge from the
				// server and can break later inbound PRE_KEY messages.
				qWarning() << "[Matrix-E2EE] preserving local OTK batch after upload collision";
			}
		} else {
			FKeysUploadCollisionRetried = false;
			FOlmCrypto.markKeysAsPublished();
			bool accountSaved = false;
			runDatabase([&](MatrixDatabase &database) {
				accountSaved = FOlmCrypto.persistAccount(database);
			});
			if (!accountSaved)
				qWarning() << "[Matrix-E2EE] failed to persist published Olm keys";
		}
		// The first sync must start after the asynchronous key upload.  The
		// login path cannot start it earlier without racing the key publish.
		if (!FSyncInFlight && !FAccesToken.isEmpty())
			sync();
	} else if (type == "own_devices") {
		if (reply->error() == QNetworkReply::NoError) {
			const QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
			const QJsonArray devices = response.value(QStringLiteral("devices")).toArray();
			qWarning() << "[Matrix-E2EE] own devices received:" << devices.size();
			// Cross-signing keys can be signed by any device. Query the complete
			// user key set once instead of validating the master key against
			// eleven incomplete single-device responses.
			if (!devices.isEmpty())
				queryDeviceKeys(FUserId);
		} else {
			qWarning() << "[Matrix-E2EE] own devices query failed:" << reply->errorString();
		}
	} else if (type == "keys_query") {
		const QString userId = reply->property("keysUserId").toString();
		const QString deviceId = reply->property("keysDeviceId").toString();
		if (reply->error() != QNetworkReply::NoError) {
			qWarning() << "Matrix device key query failed:" << reply->errorString();
		} else {
			int invalidSignatureCount = 0;
			QStringList validDeviceIds;
			const QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
			FVerificationKeyQueryResponses.insert(userId + QLatin1Char('\n') + deviceId, response);
			QJsonObject masterKey = response.value(QStringLiteral("master_keys")).toObject()
				.value(userId).toObject();
			if (masterKey.isEmpty()) {
				QByteArray storedMaster;
				bool loadedMaster = false;
				runDatabase([&](MatrixDatabase &database) {
					loadedMaster = database.loadCrossSigningKey(userId, QStringLiteral("master"), storedMaster);
				});
				if (loadedMaster)
					masterKey = QJsonDocument::fromJson(storedMaster).object();
			}
			bool masterKeyValid = false;
			QString masterKeyValue;
			QJsonObject storedMasterKey;
			bool storedMasterLoaded = false;
			runDatabase([&](MatrixDatabase &database) {
				QByteArray storedMasterJson;
				storedMasterLoaded = database.loadCrossSigningKey(userId, QStringLiteral("master"),
					storedMasterJson);
				if (storedMasterLoaded)
					storedMasterKey = QJsonDocument::fromJson(storedMasterJson).object();
			});
			if (!masterKey.isEmpty()) {
				const QJsonObject masterKeys = masterKey.value(QStringLiteral("keys")).toObject();
				const QString masterKeyId = masterKeys.size() == 1 ? masterKeys.constBegin().key() : QString();
				const QString masterSigningKeyValue = masterKeys.size() == 1
					? masterKeys.constBegin().value().toString() : QString();
				const QJsonObject deviceKeys = response.value(QStringLiteral("device_keys"))
					.toObject().value(userId).toObject();
				QJsonObject deviceKeysById = deviceKeys;
				runDatabase([&](MatrixDatabase &database) {
					const QMap<QString, QByteArray> storedDeviceKeys = database.loadDeviceKeysForUser(userId);
					for (auto storedIt = storedDeviceKeys.constBegin();
						storedIt != storedDeviceKeys.constEnd(); ++storedIt) {
						const QJsonObject storedDevice = QJsonDocument::fromJson(storedIt.value()).object();
						if (!storedDevice.isEmpty())
							deviceKeysById.insert(storedIt.key(), storedDevice);
					}
				});
				const QJsonObject masterSignatures = masterKey.value(QStringLiteral("signatures"))
					.toObject().value(userId).toObject();
				bool masterSignatureValid = false;
				int masterSignerCandidates = 0;
				int masterValidSignatures = 0;
				QStringList masterSignatureIds;
				for (auto signatureIt = masterSignatures.constBegin();
					signatureIt != masterSignatures.constEnd(); ++signatureIt) {
					const QString candidateId = signatureIt.key();
					masterSignatureIds.append(candidateId);
					if (!candidateId.startsWith(QStringLiteral("ed25519:")))
						continue;
					const QString candidateDeviceId = candidateId.mid(QStringLiteral("ed25519:").size());
					QJsonObject candidateDevice = deviceKeysById.value(candidateDeviceId).toObject();
					if (candidateDevice.isEmpty()) {
						for (auto deviceIt = deviceKeysById.constBegin();
							deviceIt != deviceKeysById.constEnd(); ++deviceIt) {
							const QJsonObject device = deviceIt.value().toObject();
							if (device.value(QStringLiteral("device_id")).toString() == candidateDeviceId) {
								candidateDevice = device;
								break;
							}
						}
					}
					const QString candidateValue = candidateDevice.value(QStringLiteral("keys"))
						.toObject().value(candidateId).toString();
					if (candidateValue.isEmpty())
						continue;
					++masterSignerCandidates;
					if (FOlmCrypto.verifyCrossSigningKey(userId, masterKey,
						candidateId, candidateValue)) {
						masterSignatureValid = true;
						++masterValidSignatures;
						break;
					}
				}
				const QJsonObject storedMasterKeys = storedMasterKey.value(QStringLiteral("keys")).toObject();
				const QString storedMasterValue = storedMasterKeys.size() == 1
					? storedMasterKeys.constBegin().value().toString() : QString();
				// Device signatures establish a master key initially. Once that
				// exact master public key was validated, deleting the signing
				// device must not revoke the cross-signing identity itself.
				const bool previouslyValidatedMaster = storedMasterLoaded &&
					!storedMasterValue.isEmpty() && storedMasterValue == masterSigningKeyValue;
				masterKeyValid = !masterKeyId.isEmpty() &&
					(masterSignatureValid || previouslyValidatedMaster);
				if (!masterKeyValid) {
					qWarning() << "[Matrix-E2EE] master cross-signing key is currently unverified;"
						<< "no current device signature is available";
					const QByteArray observedMasterJson = QJsonDocument(masterKey).toJson(QJsonDocument::Compact);
					runDatabase([&](MatrixDatabase &database) {
						database.saveCrossSigningKey(userId, QStringLiteral("master_observed"), observedMasterJson);
					});
					qWarning() << "[Matrix-E2EE] master key validation details:"
						<< "keyCount:" << masterKeys.size()
						<< "signatureIds:" << masterSignatureIds
						<< "deviceCount:" << deviceKeysById.size()
						<< "signerCandidates:" << masterSignerCandidates
						<< "validSignatures:" << masterValidSignatures
						<< "masterObjectHash:" << QCryptographicHash::hash(
							QJsonDocument(masterKey).toJson(QJsonDocument::Compact),
							QCryptographicHash::Sha256).toHex();
					if (!previouslyValidatedMaster)
						runDatabase([&](MatrixDatabase &database) {
							database.removeCrossSigningKey(userId, QStringLiteral("master"));
						});
				}
				else {
					masterKeyValue = masterSigningKeyValue;
					const QByteArray masterJson = QJsonDocument(masterKey).toJson(QJsonDocument::Compact);
					runDatabase([&](MatrixDatabase &database) {
						database.saveCrossSigningKey(userId, QStringLiteral("master"), masterJson);
					});
				}
			}
			if (masterKeyValid) {
				QJsonObject selfSigningKey = response.value(QStringLiteral("self_signing_keys"))
					.toObject().value(userId).toObject();
				if (selfSigningKey.isEmpty()) {
					QByteArray storedSelfSigning;
					bool loadedSelfSigning = false;
					runDatabase([&](MatrixDatabase &database) {
						loadedSelfSigning = database.loadCrossSigningKey(userId,
							QStringLiteral("self_signing"), storedSelfSigning);
					});
					if (loadedSelfSigning)
						selfSigningKey = QJsonDocument::fromJson(storedSelfSigning).object();
				}
				if (!selfSigningKey.isEmpty() &&
					!FOlmCrypto.verifyCrossSigningKey(userId, selfSigningKey,
						masterKey.value(QStringLiteral("keys")).toObject().constBegin().key(), masterKeyValue))
					qWarning() << "Rejected invalid Matrix self-signing key:" << userId;
				else if (!selfSigningKey.isEmpty()) {
					const QByteArray selfSigningJson = QJsonDocument(selfSigningKey).toJson(QJsonDocument::Compact);
					runDatabase([&](MatrixDatabase &database) {
						database.saveCrossSigningKey(userId, QStringLiteral("self_signing"), selfSigningJson);
					});
					const QJsonObject selfKeys = selfSigningKey.value(QStringLiteral("keys")).toObject();
					const QString selfKeyId = selfKeys.size() == 1 ? selfKeys.constBegin().key() : QString();
					const QString selfKeyValue = selfKeys.size() == 1 ? selfKeys.constBegin().value().toString() : QString();
					const QJsonObject devices = response.value(QStringLiteral("device_keys")).toObject()
						.value(userId).toObject();
					for (auto deviceIt = devices.constBegin(); deviceIt != devices.constEnd(); ++deviceIt) {
						const bool deviceChainValid = FOlmCrypto.verifyDeviceCrossSignature(userId,
							deviceIt.value().toObject(), selfKeyId, selfKeyValue);
						if (!deviceChainValid)
							qWarning() << "Matrix device has no valid self-signing signature:"
								<< userId << deviceIt.key();
						else if (userId == FUserId) {
							const QJsonObject deviceKeys = deviceIt.value().toObject()
								.value(QStringLiteral("keys")).toObject();
							const QString identityKey = deviceKeys.value(
								QStringLiteral("ed25519:%1").arg(deviceIt.key())).toString();
							runDatabase([&](MatrixDatabase &database) {
								database.saveDeviceTrust(userId, deviceIt.key(), identityKey,
									QStringLiteral("verified"));
							});
						}
					}
				}
				QJsonObject userSigningKey = response.value(QStringLiteral("user_signing_keys"))
					.toObject().value(userId).toObject();
				if (userSigningKey.isEmpty()) {
					QByteArray storedUserSigning;
					bool loadedUserSigning = false;
					runDatabase([&](MatrixDatabase &database) {
						loadedUserSigning = database.loadCrossSigningKey(userId,
							QStringLiteral("user_signing"), storedUserSigning);
					});
					if (loadedUserSigning)
						userSigningKey = QJsonDocument::fromJson(storedUserSigning).object();
				}
				if (!userSigningKey.isEmpty() &&
					!FOlmCrypto.verifyCrossSigningKey(userId, userSigningKey,
						masterKey.value(QStringLiteral("keys")).toObject().constBegin().key(), masterKeyValue))
					qWarning() << "Rejected invalid Matrix user-signing key:" << userId;
				else if (!userSigningKey.isEmpty()) {
					const QByteArray userSigningJson = QJsonDocument(userSigningKey).toJson(QJsonDocument::Compact);
					runDatabase([&](MatrixDatabase &database) {
						database.saveCrossSigningKey(userId, QStringLiteral("user_signing"), userSigningJson);
					});
				}
			}
			const QJsonObject userDevices = response.value(QStringLiteral("device_keys")).toObject()
				.value(userId).toObject();
			if (deviceId.isEmpty() && userId == FUserId) {
				const QJsonObject localDevice = userDevices.value(FDeviceId).toObject();
				const QString queriedIdentity = localDevice.value(QStringLiteral("keys")).toObject()
					.value(QStringLiteral("ed25519:%1").arg(FDeviceId)).toString();
				const QString localIdentity = QJsonDocument::fromJson(FOlmCrypto.identityKeysJson()).object()
					.value(QStringLiteral("ed25519")).toString();
				const QJsonObject currentMaster = response.value(QStringLiteral("master_keys"))
					.toObject().value(FUserId).toObject().value(QStringLiteral("keys")).toObject();
				const QString currentMasterValue = currentMaster.size() == 1
					? currentMaster.constBegin().value().toString() : QString();
				QByteArray storedMasterJson;
				bool storedMasterLoaded = false;
				runDatabase([&](MatrixDatabase &database) {
					storedMasterLoaded = database.loadCrossSigningKey(FUserId, QStringLiteral("master"),
						storedMasterJson);
				});
				const QJsonObject storedMaster = QJsonDocument::fromJson(storedMasterJson).object()
					.value(QStringLiteral("keys")).toObject();
				const QString storedMasterValue = storedMaster.size() == 1
					? storedMaster.constBegin().value().toString() : QString();
				const bool deviceKeyMatchesServer = !localIdentity.isEmpty() && localIdentity == queriedIdentity;
				if (!deviceKeyMatchesServer) {
					runDatabase([&](MatrixDatabase &database) {
						database.clearOlmSessionsForUser(FUserId);
					});
					FOlmCrypto.clearOlmSessions();
					qWarning() << "[Matrix-E2EE] cleared stale Olm sessions after local device-key change";
				}
				qWarning() << "[Matrix-E2EE] local key sync consistency:"
					<< "deviceKeyMatchesServer:" << (!localIdentity.isEmpty() && localIdentity == queriedIdentity)
					<< "masterKeyMatchesServer:" << (storedMasterLoaded && !storedMasterValue.isEmpty() &&
						storedMasterValue == currentMasterValue)
					<< "localDeviceKeyHash:" << QCryptographicHash::hash(localIdentity.toUtf8(),
						QCryptographicHash::Sha256).toHex()
					<< "queriedDeviceKeyHash:" << QCryptographicHash::hash(queriedIdentity.toUtf8(),
						QCryptographicHash::Sha256).toHex()
					<< "storedMasterKeyHash:" << QCryptographicHash::hash(storedMasterValue.toUtf8(),
						QCryptographicHash::Sha256).toHex()
					<< "queriedMasterKeyHash:" << QCryptographicHash::hash(currentMasterValue.toUtf8(),
						QCryptographicHash::Sha256).toHex();
			}
			if (!deviceId.isEmpty()) {
				const QJsonObject keys = userDevices.value(deviceId).toObject();
				if (!keys.isEmpty() && FOlmCrypto.verifyDeviceKeys(userId, deviceId, keys)) {
					validDeviceIds.append(deviceId);
					const QByteArray keysJson = QJsonDocument(keys).toJson(QJsonDocument::Compact);
					runDatabase([&](MatrixDatabase &database) {
						database.saveDeviceKeys(userId, deviceId, keysJson);
					});
					emit deviceKeysReceived(userId, deviceId, keys);
					bool keyChanged = false;
					runDatabase([&](MatrixDatabase &database) {
						keyChanged = database.deviceKeyChanged(userId, deviceId);
					});
					if (!keyChanged)
						claimOneTimeKey(userId, deviceId);
					else {
						emit deviceTrustChanged(userId, deviceId);
						qWarning() << "Matrix device key changed; withholding One-Time-Key claim:" << userId << deviceId;
					}
				} else if (!keys.isEmpty())
					++invalidSignatureCount;
			} else {
				for (auto deviceIt = userDevices.constBegin(); deviceIt != userDevices.constEnd(); ++deviceIt) {
					const QJsonObject keys = deviceIt.value().toObject();
					if (keys.isEmpty() || !FOlmCrypto.verifyDeviceKeys(userId, deviceIt.key(), keys)) {
						if (!keys.isEmpty())
							++invalidSignatureCount;
						continue;
					}
					const QString returnedDeviceId = deviceIt.key();
					validDeviceIds.append(returnedDeviceId);
					const QByteArray keysJson = QJsonDocument(keys).toJson(QJsonDocument::Compact);
					runDatabase([&](MatrixDatabase &database) {
						database.saveDeviceKeys(userId, returnedDeviceId, keysJson);
					});
					emit deviceKeysReceived(userId, returnedDeviceId, keys);
					bool keyChanged = false;
					runDatabase([&](MatrixDatabase &database) {
						keyChanged = database.deviceKeyChanged(userId, returnedDeviceId);
					});
					if (!keyChanged)
						claimOneTimeKey(userId, returnedDeviceId);
					else {
						emit deviceTrustChanged(userId, returnedDeviceId);
						qWarning() << "Matrix device key changed; withholding One-Time-Key claim:"
							<< userId << returnedDeviceId;
					}
				}
			if (deviceId.isEmpty() && invalidSignatureCount == 0) {
				bool reconciled = false;
				runDatabase([&](MatrixDatabase &database) {
					reconciled = database.removeDeviceKeysExcept(userId, validDeviceIds);
				});
				if (!reconciled)
					qWarning() << "Failed to reconcile Matrix device keys:" << userId;
				else
					qWarning() << "[Matrix-E2EE] device keys reconciled:" << userId
						<< "current:" << validDeviceIds.size();
			}
			if (invalidSignatureCount > 0)
				qWarning() << "Rejected invalid Matrix device-key signatures:"
					<< userId << "count:" << invalidSignatureCount;
		}
		for (auto pendingIt = FPendingVerificationMacs.constBegin();
			pendingIt != FPendingVerificationMacs.constEnd(); ++pendingIt) {
			const QJsonObject pendingEvent = pendingIt.value();
			const QString pendingSender = pendingEvent.value(QStringLiteral("sender")).toString();
			const QString pendingDevice = pendingEvent.value(QStringLiteral("content"))
				.toObject().value(QStringLiteral("from_device")).toString();
			if (pendingSender == userId && (deviceId.isEmpty() || pendingDevice == deviceId))
				FReadyVerificationMacs.insert(pendingIt.key());
		}
		FDeviceKeyQueries.remove(userId);
		emitRosterSnapshot();
	}
	} else if (type == "keys_claim") {
		const QString userId = reply->property("keysUserId").toString();
		const QString deviceId = reply->property("keysDeviceId").toString();
		if (reply->error() != QNetworkReply::NoError) {
			qWarning() << "[Matrix-E2EE] one-time key claim failed:" << userId << deviceId
				<< "status:" << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
				<< "error:" << reply->errorString();
		} else {
			const QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
			const QJsonObject keys = response.value(QStringLiteral("one_time_keys")).toObject()
				.value(userId).toObject().value(deviceId).toObject();
			if (!keys.isEmpty()) {
				const QString keyId = keys.constBegin().key();
				const QJsonObject oneTimeKey = keys.constBegin().value().toObject();
				const QString key = oneTimeKey.value(QStringLiteral("key")).toString();
				qWarning() << "[Matrix-E2EE] received OTK claim:" << userId << deviceId
					<< "key_id:" << keyId;
				QByteArray deviceKeysJson;
				bool deviceKeysLoaded = false;
				runDatabase([&](MatrixDatabase &database) {
					deviceKeysLoaded = database.loadDeviceKeys(userId, deviceId, deviceKeysJson);
				});
				const QString signingKey = QJsonDocument::fromJson(deviceKeysJson).object()
					.value(QStringLiteral("keys")).toObject()
					.value(QStringLiteral("ed25519:%1").arg(deviceId)).toString();
				if (!deviceKeysLoaded || !FOlmCrypto.verifySignedOneTimeKey(userId, deviceId,
					signingKey, oneTimeKey)) {
					qWarning() << "[Matrix-E2EE] rejected unsigned or invalid one-time key claim:"
						<< userId << deviceId;
				} else if (!keyId.isEmpty() && !key.isEmpty()) {
					emit oneTimeKeyReceived(userId, deviceId, keyId, key);
				}
			} else {
				qWarning() << "[Matrix-E2EE] one-time key claim returned no key:" << userId << deviceId;
			}
		}
	} else if (type == "ssss_default") {
		if (reply->error() != QNetworkReply::NoError) {
			const QSet<QString> pendingSecrets = FSsssPendingSecrets;
			for (const QString &secret : pendingSecrets)
				requestSsssSecretFromDevices(secret);
		} else {
			FSsssKeyId = QJsonDocument::fromJson(reply->readAll()).object()
				.value(QStringLiteral("key")).toString();
			if (FSsssKeyId.isEmpty()) {
				const QSet<QString> pendingSecrets = FSsssPendingSecrets;
				for (const QString &secret : pendingSecrets)
					requestSsssSecretFromDevices(secret);
			} else
				requestSsssKeyDescription(FSsssKeyId);
		}
	} else if (type == "ssss_key") {
		if (reply->error() != QNetworkReply::NoError) {
			const QSet<QString> pendingSecrets = FSsssPendingSecrets;
			for (const QString &secret : pendingSecrets)
				requestSsssSecretFromDevices(secret);
		} else {
			FSsssKeyDescription = QJsonDocument::fromJson(reply->readAll()).object();
			if (!MatrixSsss::derivePassphraseKey(FSsssRecoveryInput, FSsssKeyDescription,
				FSsssDerivedKey) &&
				!MatrixSsss::deriveRecoveryKey(FSsssRecoveryInput, FSsssKeyDescription,
					FSsssDerivedKey)) {
				emit ssssRecoveryFinished(false, QStringLiteral("SSSS input could not unlock the key"));
			} else {
				const QSet<QString> pendingSecrets = FSsssPendingSecrets;
				for (const QString &secret : pendingSecrets)
					requestSsssSecret(secret);
			}
		}
	} else if (type == "ssss_secret") {
		const QString secretName = reply->property("ssssName").toString();
		if (reply->error() != QNetworkReply::NoError) {
			requestSsssSecretFromDevices(secretName);
		} else {
			const QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
			const QJsonObject encrypted = response.value(QStringLiteral("encrypted")).toObject()
				.value(FSsssKeyId).toObject();
			QByteArray plaintext;
			if (encrypted.isEmpty() || !MatrixSsss::decryptSecret(encrypted, FSsssDerivedKey,
				secretName, plaintext)) {
				emit ssssRecoveryFinished(false, QStringLiteral("SSSS secret decryption failed"));
			} else {
				FSsssSecrets.insert(secretName, plaintext);
				FSsssPendingSecrets.remove(secretName);
				if (FSsssPendingSecrets.isEmpty()) {
					if (!validateRecoveredMasterSecret()) {
						FSsssSecrets.clear();
						emit ssssRecoveryFinished(false, QStringLiteral("SSSS master key does not match the server master key"));
						return;
					}
					uploadRecoveredMasterKeySignature();
					uploadOwnDeviceSignature(FSsssSecrets.value(QStringLiteral("m.cross_signing.self_signing")));
					FSsssRecoveryInput.clear();
					FSsssKeyId.clear();
					FSsssKeyDescription = QJsonObject();
					FSsssDerivedKey.clear();
					emit ssssRecoveryFinished(true, QString());
				}
			}
		}
	} else if (type == "signatures_upload") {
		const QByteArray responseBody = reply->readAll();
		qWarning() << "[Matrix-E2EE] signatures upload result:"
			<< "status:" << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
			<< "error:" << (reply->error() == QNetworkReply::NoError
				? QStringLiteral("none") : reply->errorString())
			<< "body:" << responseBody;
	} else if (type == "to_device") {
		const QByteArray responseBody = reply->readAll();
		const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		qWarning() << "[Matrix-E2EE] to-device HTTP result:"
			<< reply->property("e2eeEventType").toString()
			<< "status:" << status
			<< "error:" << (reply->error() == QNetworkReply::NoError
				? QStringLiteral("none") : reply->errorString())
			<< "body:" << responseBody;
		if (reply->property("e2eeOlmRecovery").toBool() && status >= 200 && status < 300) {
			const QString recoveryUser = reply->property("e2eeRecoveryUserId").toString();
			const QString recoveryDevice = reply->property("e2eeRecoveryDeviceId").toString();
			if (!recoveryUser.isEmpty() && !recoveryDevice.isEmpty()) {
				qWarning() << "[Matrix-E2EE] m.dummy accepted; retrying pending room-key requests:"
					<< recoveryUser << recoveryDevice;
				const QStringList pendingKeys = FRoomKeyRequestIds.keys();
				for (const QString &requestKey : pendingKeys) {
					const QStringList parts = requestKey.split(QLatin1Char('\n'));
					if (parts.size() != 3 || FRoomKeyRequestUsers.value(requestKey) != recoveryUser)
						continue;
					FRequestedRoomKeys.remove(requestKey);
					FRequestedRoomKeyTimes.remove(requestKey);
					requestMissingRoomKey(FRoomKeyRequestUsers.value(requestKey),
						parts.at(1), parts.at(0), parts.at(2), recoveryDevice);
				}
			}
		}
	}
	
	// Clean up reply after routing
	reply->deleteLater();
}

void MatrixNetwork::uploadOwnMasterKeySignature(const QJsonObject &masterKey)
{
	if (FAccesToken.isEmpty() || FUserId.isEmpty() || FDeviceId.isEmpty() || masterKey.isEmpty())
		return;
	if (masterKey.value(QStringLiteral("user_id")).toString() != FUserId)
		return;
	const QJsonObject keys = masterKey.value(QStringLiteral("keys")).toObject();
	if (keys.size() != 1)
		return;
	const QString masterKeyValue = keys.constBegin().value().toString();
	if (masterKeyValue.isEmpty())
		return;
	QJsonObject signedMaster = masterKey;
	signedMaster.remove(QStringLiteral("signatures"));
	signedMaster.remove(QStringLiteral("unsigned"));
	const QByteArray signature = FOlmCrypto.signCanonicalJson(FOlmCrypto.canonicalJson(signedMaster));
	if (signature.isEmpty())
		return;
	QJsonObject userSignatures;
	userSignatures.insert(QStringLiteral("ed25519:%1").arg(FDeviceId),
		QString::fromLatin1(signature));
	signedMaster.insert(QStringLiteral("signatures"), QJsonObject{{FUserId, userSignatures}});
	const QJsonObject identity = QJsonDocument::fromJson(FOlmCrypto.identityKeysJson()).object();
	const QString deviceSigningKey = identity.value(QStringLiteral("ed25519")).toString();
	if (deviceSigningKey.isEmpty() || !FOlmCrypto.verifyCrossSigningKey(FUserId, signedMaster,
		QStringLiteral("ed25519:%1").arg(FDeviceId), deviceSigningKey)) {
		qWarning() << "[Matrix-E2EE] refusing locally invalid master-key signature upload";
		return;
	}
	const QJsonObject payload{{FUserId, QJsonObject{{masterKeyValue, signedMaster}}}};
	QNetworkRequest request(QUrl(constructUrl(QStringLiteral("/_matrix/client/v3/keys/signatures/upload"))));
	request.setAttribute(QNetworkRequest::Http2AllowedAttribute, FUseHttp2);
	request.setRawHeader("Authorization", QByteArray("Bearer ") + FAccesToken.toUtf8());
	request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
	QNetworkReply *reply = FNetworkAccessManager->post(request,
		QJsonDocument(payload).toJson(QJsonDocument::Compact));
	reply->setProperty("requestType", QStringLiteral("signatures_upload"));
}

void MatrixNetwork::onLoginFinished(QNetworkReply *reply)
{
	Q_UNUSED(reply);
	
	if (FInFlightRequest != RequestLogin) {
		// Not waiting for a login response
		return;
	}
	
	setRequestType(RequestNone);
	
	if (reply->error() != QNetworkReply::NoError) {
		QString error = reply->errorString();
		const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		qWarning() << "Matrix login failed:" << error << "HTTP status:" << httpStatus;
		emit loginError(error);
		emit connectionStateChanged(0);
		return;
	}
	
	QByteArray response = reply->readAll();
	const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
	if (httpStatus >= 400)
	{
		const QJsonDocument errorDoc = QJsonDocument::fromJson(response);
		const QJsonObject errorObject = errorDoc.object();
		const int retryAfter = errorObject.value(QStringLiteral("retry_after_ms")).toInt();
		qWarning() << "Matrix login HTTP failure:" << httpStatus
			<< "response bytes:" << response.size()
			<< "retry_after_ms:" << retryAfter;
		emit loginError(QStringLiteral("Matrix login failed (HTTP %1)").arg(httpStatus));
		emit connectionStateChanged(0);
		return;
	}
	if (response.isEmpty()) {
		emit loginError("Empty login response from server");
		emit connectionStateChanged(0);
		return;
	}
	
	// Handle malformed JSON
	QJsonParseError parseError;
	QJsonDocument doc = QJsonDocument::fromJson(response, &parseError);
	if (parseError.error != QJsonParseError::NoError) {
		QString errorMsg = QString("Malformed JSON in login response: %1").arg(parseError.errorString());
		qWarning() << errorMsg;
		emit loginError(errorMsg);
		emit connectionStateChanged(0);
		return;
	}
	
	QJsonObject obj = doc.object();
	QString userId = obj.value("user_id").toString();
	QString accessToken = obj.value("access_token").toString();
	QString deviceId = obj.value("device_id").toString();  // Good practice to record device_id
	
	// Validate response
	if (userId.isEmpty() || accessToken.isEmpty()) {
		QString errorMsg = "Matrix login response lacks user_id or access_token";
		qWarning() << errorMsg;
		emit loginError(errorMsg);
		emit connectionStateChanged(0);
		return;
	}
	
	FReplacedRoomIds.clear();
	FRooms.clear();
	FDisplayNameRequests.clear();
	FAvatarRetries.clear();
	FAvatarLegacyFallbacks.clear();
	FAvatarRequestsInFlight.clear();
	FAvatarUnavailable.clear();
	FRoomNameRequests.clear();
	FJoinedMembersRequests.clear();
	FJoinedMembersLoaded.clear();
	FDeviceKeyQueries.clear();
	FPendingEncryptedMessages.clear();
	FSasPublicKeys.clear();
	FSasTheirPublicKeys.clear();
	FSasInitiatorUsers.clear();
	FSasInitiatorDevices.clear();
	FSasPeerDevices.clear();
	FSasStartContents.clear();
	FSasCommitments.clear();
	FE2EESyncDiagnosticEmitted = false;
	
	FAccesToken = accessToken;
	FDeviceId = deviceId;
	FUserId = userId;
	FInitialSyncComplete = false;
	FKeysUploadInFlight = false;
	bool databaseOpened = false;
	if (FDatabaseWorker) {
		QMetaObject::invokeMethod(FDatabaseWorker, "openForAccount", Qt::BlockingQueuedConnection,
			Q_RETURN_ARG(bool, databaseOpened),
			Q_ARG(QString, FDatabaseProfileDirectory), Q_ARG(QString, FNormalizedServerUrl),
			Q_ARG(QString, FUserId));
	}
	if (!databaseOpened) {
		const QString error = QStringLiteral("Matrix SQLite database could not be opened");
		qCritical() << error;
		emit loginError(error);
		emit connectionStateChanged(0);
		return;
	}
	bool localOlmContextChanged = false;
	bool localOlmContextKnown = false;
	runDatabase([&](MatrixDatabase &database) {
		localOlmContextKnown = database.ensureOlmSessionContext(FUserId, FDeviceId,
			localOlmContextChanged);
		if (localOlmContextKnown && localOlmContextChanged)
			localOlmContextKnown = database.clearOlmSessionsForUser(FUserId);
	});
	if (!localOlmContextKnown) {
		qCritical() << "[Matrix-E2EE] failed to initialize local Olm session context";
		emit loginError(QStringLiteral("Matrix Olm session context could not be initialized"));
		emit connectionStateChanged(0);
		return;
	}
	if (localOlmContextChanged)
		qWarning() << "[Matrix-E2EE] local device context changed; discarded persisted remote Olm sessions";
	if (qEnvironmentVariableIsSet("VACUUM_MATRIX_CLEAR_SYNC_CURSOR")) {
		bool cleared = false;
		if (FDatabaseWorker)
			QMetaObject::invokeMethod(FDatabaseWorker, "clearNextBatch", Qt::BlockingQueuedConnection,
				Q_RETURN_ARG(bool, cleared));
		Q_UNUSED(cleared);
	}
	if (FDatabaseWorker)
		QMetaObject::invokeMethod(FDatabaseWorker, "directRooms", Qt::BlockingQueuedConnection,
			Q_RETURN_ARG(QSet<QString>, FDirectRoomIds));
	FHasDirectRoomData = !FDirectRoomIds.isEmpty();
	restorePersistedRooms();
	// Resume from the last atomically persisted Matrix cursor after the
	// account-specific database connection has been opened.
	if (FDatabaseWorker) {
		QMetaObject::invokeMethod(FDatabaseWorker, "nextBatch", Qt::BlockingQueuedConnection,
			Q_RETURN_ARG(QString, FSyncToken));
		QMetaObject::invokeMethod(FDatabaseWorker, "syncFilter", Qt::BlockingQueuedConnection,
			Q_RETURN_ARG(QString, FSyncFilterId));
	}
	FFilterCreationAttempted = false;
	QByteArray olmPickle;
	QByteArray olmPickleKey;
	bool olmAccountLoaded = false;
	bool olmAccountExists = false;
	runDatabase([&](MatrixDatabase &database) {
		olmAccountExists = database.hasOlmAccount(FUserId, FDeviceId);
		olmAccountLoaded = database.loadOlmAccount(FUserId, FDeviceId, olmPickleKey, olmPickle);
	});
	bool olmInitialized = false;
	if (olmAccountLoaded)
		olmInitialized = FOlmCrypto.initializeFromStoredAccount(FUserId, FDeviceId,
			olmPickleKey, olmPickle);
	if (!olmInitialized && olmAccountExists) {
		qCritical() << "[Matrix-E2EE] persisted Olm account exists but could not be loaded;"
			<< "refusing to replace it with a new account";
	} else if (!olmInitialized) {
		olmInitialized = FOlmCrypto.initializeNewAccount(FUserId, FDeviceId,
			olmPickleKey, olmPickle);
		if (olmInitialized) {
			runDatabase([&](MatrixDatabase &database) {
				olmInitialized = database.saveOlmAccount(FUserId, FDeviceId,
					olmPickleKey, olmPickle);
			});
			QByteArray verifyPickleKey;
			QByteArray verifyPickle;
			bool persisted = false;
			runDatabase([&](MatrixDatabase &database) {
				persisted = database.loadOlmAccount(FUserId, FDeviceId,
					verifyPickleKey, verifyPickle);
			});
			if (!persisted)
				qCritical() << "[Matrix-E2EE] newly created Olm account was not persisted";
			olmInitialized = olmInitialized && persisted;
		}
	}
	if (!olmInitialized)
		qWarning() << "Matrix Olm account initialization failed";
	else {
		const QJsonObject identity = QJsonDocument::fromJson(FOlmCrypto.identityKeysJson()).object();
		const QString curveKey = identity.value(QStringLiteral("curve25519")).toString();
		qWarning() << "[Matrix-E2EE] Olm account"
			<< (olmAccountLoaded ? "restored" : "created")
			<< "device_id:" << FDeviceId
			<< "curve25519_hash:" << QCryptographicHash::hash(curveKey.toUtf8(),
				QCryptographicHash::Sha256).toHex()
			<< "local_otk_count:" << FOlmCrypto.oneTimeKeyIds().size()
			<< "local_otk_ids:" << FOlmCrypto.oneTimeKeyIds();
	}
	retryFailedOutbox();
	// Restore room history lazily when a conversation is opened. Loading the
	// complete timeline here blocks the GUI for large profile databases.
	FMessageHistory.clear();
	FHistoryLoadedRooms.clear();
	setPresence(FUserId, QStringLiteral("online"));
	emit loginSuccess(userId, accessToken, deviceId);
	queryOwnDevices();
	emit connectionStateChanged(2);  // Connected state
	
	// A new account has no server-side keys and must publish an initial
	// batch before syncing.  A restored account must sync first so the
	// server's current OTK count drives the top-up; blindly generating 50
	// on every restart can evict private OTKs which are still claimable.
	if (FOlmCrypto.isInitialized() && !olmAccountLoaded)
		uploadOlmKeys();
	else
		sync();
}

void MatrixNetwork::uploadOlmKeys(int generateCount, bool replaceFallback)
{
	if (FKeysUploadInFlight || (generateCount <= 0 && !replaceFallback))
		return;
	const QByteArray body = FOlmCrypto.prepareKeysUpload(FUserId, FDeviceId,
		generateCount > 0 ? generateCount : 0, replaceFallback);
	if (body.isEmpty()) {
		qWarning() << "Matrix Olm key upload payload could not be prepared";
		sync();
		return;
	}
	const QJsonObject uploadObject = QJsonDocument::fromJson(body).object();
	const QStringList uploadedOtkIds = uploadObject.value(QStringLiteral("one_time_keys"))
		.toObject().keys();
	const QStringList uploadedFallbackIds = uploadObject.value(QStringLiteral("fallback_keys"))
		.toObject().keys();
	qWarning() << "[Matrix-E2EE] uploading OTK batch:" << FUserId << FDeviceId
		<< "count:" << uploadedOtkIds.size() << "key_ids:" << uploadedOtkIds
		<< "fallback_ids:" << uploadedFallbackIds;
	bool accountSaved = false;
	runDatabase([&](MatrixDatabase &database) {
		accountSaved = FOlmCrypto.persistAccount(database);
	});
	if (!accountSaved) {
		qWarning() << "[Matrix-E2EE] refusing Olm key upload: generated account state was not persisted";
		sync();
		return;
	}
	FKeysUploadInFlight = true;
	QNetworkRequest request(QUrl(constructUrl(QStringLiteral("/_matrix/client/v3/keys/upload"))));
	request.setAttribute(QNetworkRequest::Http2AllowedAttribute, FUseHttp2);
	request.setRawHeader("Authorization", QByteArray("Bearer ") + FAccesToken.toUtf8());
	request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
	QNetworkReply *reply = FNetworkAccessManager->post(request, body);
	reply->setProperty("requestType", QStringLiteral("keys_upload"));
}

void MatrixNetwork::queryDeviceKeys(const QString &userId, const QString &deviceId)
{
	if (FAccesToken.isEmpty() || userId.isEmpty())
		return;
	QJsonObject devices;
	devices.insert(userId, deviceId.isEmpty() ? QJsonArray() : QJsonArray{deviceId});
	QJsonObject body;
	body.insert(QStringLiteral("device_keys"), devices);
	QNetworkRequest request(QUrl(constructUrl(QStringLiteral("/_matrix/client/v3/keys/query"))));
	request.setAttribute(QNetworkRequest::Http2AllowedAttribute, FUseHttp2);
	request.setRawHeader("Authorization", QByteArray("Bearer ") + FAccesToken.toUtf8());
	request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
	QNetworkReply *reply = FNetworkAccessManager->post(request,
		QJsonDocument(body).toJson(QJsonDocument::Compact));
	reply->setProperty("requestType", QStringLiteral("keys_query"));
	reply->setProperty("keysUserId", userId);
	reply->setProperty("keysDeviceId", deviceId);
	}

	void MatrixNetwork::queryOwnDevices()
	{
	if (FAccesToken.isEmpty() || FUserId.isEmpty())
	return;
	QNetworkRequest request(QUrl(constructUrl(QStringLiteral("/_matrix/client/v3/devices"))));
	request.setAttribute(QNetworkRequest::Http2AllowedAttribute, FUseHttp2);
	request.setRawHeader("Authorization", QByteArray("Bearer ") + FAccesToken.toUtf8());
	QNetworkReply *reply = FNetworkAccessManager->get(request);
	reply->setProperty("requestType", QStringLiteral("own_devices"));
	}

	void MatrixNetwork::claimOneTimeKey(const QString &userId, const QString &deviceId)
{
	if (FAccesToken.isEmpty() || userId.isEmpty() || deviceId.isEmpty())
		return;
	// Never consume the local device's own server-side OTK.  Doing so wastes
	// the key and creates a bogus self-session instead of an Olm channel to a
	// different device.
	if (userId == FUserId && deviceId == FDeviceId)
		return;
	QJsonObject devices;
	devices.insert(userId, QJsonObject{{deviceId, QStringLiteral("signed_curve25519")}});
	QJsonObject body;
	body.insert(QStringLiteral("one_time_keys"), devices);
	QNetworkRequest request(QUrl(constructUrl(QStringLiteral("/_matrix/client/v3/keys/claim"))));
	request.setAttribute(QNetworkRequest::Http2AllowedAttribute, FUseHttp2);
	request.setRawHeader("Authorization", QByteArray("Bearer ") + FAccesToken.toUtf8());
	request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
	QNetworkReply *reply = FNetworkAccessManager->post(request,
		QJsonDocument(body).toJson(QJsonDocument::Compact));
	reply->setProperty("requestType", QStringLiteral("keys_claim"));
	reply->setProperty("keysUserId", userId);
	reply->setProperty("keysDeviceId", deviceId);
	qWarning() << "[Matrix-E2EE] claiming one-time key:" << userId << deviceId;
}

void MatrixNetwork::onSyncFinished(QNetworkReply *reply)
{
	FSyncInFlight = false;
	const bool wasInitialSync = !FInitialSyncComplete;
	if (reply->error() != QNetworkReply::NoError) {
		QString error = reply->errorString();
		const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (reply->property("localSyncTimeout").toBool() &&
			reply->error() == QNetworkReply::OperationCanceledError) {
			scheduleSyncRetry(100);
			return;
		}
		const QByteArray errorBody = reply->readAll();
		const QJsonObject errorJson = QJsonDocument::fromJson(errorBody).object();
		int retryDelayMs = -1;
		if (httpStatus == 429) {
			retryDelayMs = errorJson.value(QStringLiteral("retry_after_ms")).toInt(-1);
			if (retryDelayMs < 0) {
				bool retryAfterOk = false;
				const int retryAfterSeconds = reply->rawHeader("Retry-After").toInt(&retryAfterOk);
				if (retryAfterOk)
					retryDelayMs = retryAfterSeconds * 1000;
			}
			if (retryDelayMs < 0)
				retryDelayMs = 1000;
			retryDelayMs = qBound(100, retryDelayMs, 120000);
		}
		qWarning() << "Matrix sync request failed:" << error
			<< "httpStatus:" << httpStatus
			<< "http2:" << FUseHttp2 << "errcode:"
			<< errorJson.value(QStringLiteral("errcode")).toString()
 << "retryDelayMs:" << retryDelayMs;
 const QString errcode = errorJson.value(QStringLiteral("errcode")).toString();
 if (!FSyncFilterId.isEmpty() &&
 (errcode == QStringLiteral("M_UNKNOWN") ||
  errcode == QStringLiteral("M_NOT_FOUND") ||
  errcode == QStringLiteral("M_INVALID_PARAM"))) {
 FSyncFilterId.clear();
 FFilterCreationAttempted = false;
 if (FDatabaseWorker)
	QMetaObject::invokeMethod(FDatabaseWorker, "removeSyncFilter", Qt::QueuedConnection);
 scheduleSyncRetry(100);
 return;
 }
 emit syncError(error);
		if ((httpStatus == 401 || httpStatus == 403) &&
			errcode == QStringLiteral("M_UNKNOWN_TOKEN")) {
			emit loginError(QStringLiteral("Matrix access token is no longer valid"));
			logout();
			return;
		}
		if (httpStatus == 401 || httpStatus == 403)
			return;
		if (retryDelayMs >= 0) {
			scheduleSyncRetry(retryDelayMs);
			return;
		}
		if (reply->error() == QNetworkReply::RemoteHostClosedError) {
			FUseHttp2 = !FUseHttp2;
			scheduleSyncRetry();
		}
		else
			scheduleSyncRetry();
		return;
	}
	
	QByteArray response = reply->readAll();

	if (response.isEmpty()) {
		emit syncError("Empty sync response from server");
		scheduleSyncRetry();
		return;
	}
	
	// Handle malformed JSON
	QJsonParseError parseError;
	QJsonDocument doc = QJsonDocument::fromJson(response, &parseError);
	if (parseError.error != QJsonParseError::NoError) {
		QString errorMsg = QString("Malformed JSON in sync response: %1").arg(parseError.errorString());
		qWarning() << errorMsg;
		emit syncError(errorMsg);
		scheduleSyncRetry();
		return;
	}
	
	QJsonObject obj = doc.object();
	if (!FE2EESyncDiagnosticEmitted) {
		qWarning() << "[Matrix-E2EE] sync handler active; to_device section:"
			<< obj.contains(QStringLiteral("to_device"));
		FE2EESyncDiagnosticEmitted = true;
	}
	QString nextBatch = obj.value("next_batch").toString();
	QList<MatrixTextEvent> events;
	const QJsonObject rooms = obj.value("rooms").toObject();
	const QJsonObject joined = rooms.value("join").toObject();

	const QJsonObject invited = rooms.value(QStringLiteral("invite")).toObject();
	for (auto inviteIt = invited.constBegin(); inviteIt != invited.constEnd(); ++inviteIt) {
		const QString roomId = inviteIt.key();
		ProtocolRoom &room = FRooms[roomId];
		room.id = roomId;
		room.membership = QStringLiteral("invite");
		room.isJoined = false;
		room.isAvailable = true;
		const QJsonArray inviteEvents = inviteIt.value().toObject()
			.value(QStringLiteral("invite_state")).toObject()
			.value(QStringLiteral("events")).toArray();
		for (const QJsonValue &eventValue : inviteEvents) {
			const QJsonObject stateEvent = eventValue.toObject();
			const QString stateType = stateEvent.value(QStringLiteral("type")).toString();
			const QJsonObject content = stateEvent.value(QStringLiteral("content")).toObject();
			if (stateType == QStringLiteral("m.room.name"))
				room.name = content.value(QStringLiteral("name")).toString();
			else if (stateType == QStringLiteral("m.room.topic"))
				room.subject = content.value(QStringLiteral("topic")).toString();
			else if (stateType == QStringLiteral("m.room.avatar"))
				room.avatarUrl = content.value(QStringLiteral("url")).toString();
		}
		bool inviteSaved = false;
		runDatabase([&](MatrixDatabase &database) {
			inviteSaved = database.saveRoomState(roomId, room.name, room.subject,
				room.avatarUrl, QStringLiteral("invite"), room.isDirect, room.isEncrypted,
				QString(), QString());
		});
		if (!inviteSaved)
			qWarning() << "Failed to persist Matrix invite room" << roomId;
	}
	if (!invited.isEmpty())
		emitRosterSnapshot();
	const QJsonObject deviceLists = obj.value(QStringLiteral("device_lists")).toObject();
	const QJsonObject oneTimeKeyCounts = obj.value(QStringLiteral("device_one_time_keys_count"))
		.toObject();
	const int signedCurve25519Count = oneTimeKeyCounts
		.value(QStringLiteral("signed_curve25519")).toInt(-1);
	const bool fallbackKeyNeedsReplacement = !obj.value(QStringLiteral("device_unused_fallback_key_types"))
		.toArray().contains(QStringLiteral("signed_curve25519"));
	if (signedCurve25519Count >= 0 && signedCurve25519Count < 10 && !FKeysUploadInFlight) {
		qWarning() << "[Matrix-E2EE] low one-time-key count:" << signedCurve25519Count
			<< "requesting replenishment";
		uploadOlmKeys(50 - signedCurve25519Count, fallbackKeyNeedsReplacement);
	} else if (fallbackKeyNeedsReplacement && !FKeysUploadInFlight) {
		qWarning() << "[Matrix-E2EE] fallback OTK is not unused; rotating fallback key";
		uploadOlmKeys(0, true);
	}
	for (const QJsonValue &changedValue : deviceLists.value(QStringLiteral("changed")).toArray()) {
		const QString changedUser = changedValue.toString();
		if (!changedUser.isEmpty() && !FDeviceKeyQueries.contains(changedUser)) {
			FDeviceKeyQueries.insert(changedUser);
			queryDeviceKeys(changedUser);
		}
	}
	for (const QJsonValue &leftValue : deviceLists.value(QStringLiteral("left")).toArray()) {
		const QString leftUser = leftValue.toString();
		if (!leftUser.isEmpty()) {
			FDeviceKeyQueries.remove(leftUser);
			bool keysRemoved = false;
			runDatabase([&](MatrixDatabase &database) {
				keysRemoved = database.removeDeviceKeysForUser(leftUser);
			});
			if (!keysRemoved)
				qWarning() << "Failed to remove Matrix device keys for departed user" << leftUser;
		}
	}
	const QJsonObject left = rooms.value("leave").toObject();
	for (auto leftIt = left.constBegin(); leftIt != left.constEnd(); ++leftIt) {
		const QString roomId = leftIt.key();
		const ProtocolRoom room = FRooms.value(roomId);
		bool roomSaved = false;
		runDatabase([&](MatrixDatabase &database) {
			roomSaved = database.saveRoomState(roomId, room.name, room.subject,
				room.avatarUrl, QStringLiteral("leave"), room.isDirect, room.isEncrypted,
				QString(), FRoomPrevBatch.value(roomId));
		});
		if (!roomSaved)
			qWarning() << "Failed to persist Matrix room leave" << roomId;
		bool memberSaved = false;
		runDatabase([&](MatrixDatabase &database) {
			memberSaved = database.saveRoomMember(roomId, FUserId,
				QStringLiteral("leave"), QString(), QString(), QString());
		});
		if (!memberSaved)
			qWarning() << "Failed to persist Matrix leave membership" << roomId;
		FRooms.remove(leftIt.key());
	}
	if (!left.isEmpty())
		emitRosterSnapshot();
	const QJsonObject accountData = obj.value("account_data").toObject();
	for (const QJsonValue &accountEventValue : accountData.value("events").toArray()) {
		const QJsonObject accountEvent = accountEventValue.toObject();
		const QString accountEventType = accountEvent.value("type").toString();
		if (accountEventType == QStringLiteral("m.push_rules")) {
			FHighlightBodyPatterns.clear();
			FHighlightUserPatterns.clear();
			const QJsonObject global = accountEvent.value("content").toObject().value("global").toObject();
			for (const QString &scope : {QStringLiteral("override"), QStringLiteral("content")}) {
				for (const QJsonValue &ruleValue : global.value(scope).toArray()) {
					const QJsonObject rule = ruleValue.toObject();
					bool highlights = false;
					for (const QJsonValue &actionValue : rule.value("actions").toArray()) {
						const QJsonObject tweak = actionValue.toObject();
						if (tweak.value("set_tweak").toString() == QStringLiteral("highlight") &&
							tweak.value("value").toBool())
							highlights = true;
					}
					if (!highlights)
						continue;
					for (const QJsonValue &conditionValue : rule.value("conditions").toArray()) {
						const QJsonObject condition = conditionValue.toObject();
						const QString key = condition.value("key").toString();
						const QString pattern = condition.value("pattern").toString();
						if (key == QStringLiteral("content.body") && !pattern.isEmpty())
							FHighlightBodyPatterns.append(pattern);
						else if ((key == QStringLiteral("sender") || key == QStringLiteral("user_id")) && !pattern.isEmpty())
							FHighlightUserPatterns.append(pattern);
					}
				}
			}
			continue;
		}
		if (accountEventType != QStringLiteral("m.direct"))
			continue;
		FHasDirectRoomData = true;
		FDirectRoomIds.clear();
		const QJsonObject directMap = accountEvent.value("content").toObject();
		for (auto userIt = directMap.constBegin(); userIt != directMap.constEnd(); ++userIt)
			for (const QJsonValue &roomValue : userIt.value().toArray())
				FDirectRoomIds.insert(roomValue.toString());
		bool directRoomsSaved = false;
		runDatabase([&](MatrixDatabase &database) {
			directRoomsSaved = database.saveDirectRooms(FDirectRoomIds);
		});
		if (!directRoomsSaved)
			qWarning() << "Failed to persist Matrix direct-room mapping";
	}
	const QJsonObject toDevice = obj.value(QStringLiteral("to_device")).toObject();
	QJsonArray toDeviceEvents = toDevice.value(QStringLiteral("events")).toArray();
	for (auto pendingIt = FPendingVerificationMacs.begin();
		pendingIt != FPendingVerificationMacs.end();) {
		if (!FReadyVerificationMacs.contains(pendingIt.key())) {
			++pendingIt;
			continue;
		}
		toDeviceEvents.append(pendingIt.value());
		FReadyVerificationMacs.remove(pendingIt.key());
		pendingIt = FPendingVerificationMacs.erase(pendingIt);
	}
	if (!toDeviceEvents.isEmpty())
		qWarning() << "[Matrix-E2EE] to-device events received:" << toDeviceEvents.size();
	for (const QJsonValue &toDeviceValue : toDeviceEvents) {
		const QJsonObject toDeviceEvent = toDeviceValue.toObject();
		QString toDeviceType = toDeviceEvent.value(QStringLiteral("type")).toString();
		QJsonObject content = toDeviceEvent.value(QStringLiteral("content")).toObject();
		QString toDeviceSender = toDeviceEvent.value(QStringLiteral("sender")).toString();
		QString decryptedSenderEd25519;
		QString decryptedSenderDeviceId;
		if (toDeviceType == QStringLiteral("m.secret.request")) {
			const QString sender = toDeviceEvent.value(QStringLiteral("sender")).toString();
			const QString action = content.value(QStringLiteral("action")).toString();
			const QString deviceId = content.value(QStringLiteral("requesting_device_id")).toString();
			const QString secretName = content.value(QStringLiteral("name")).toString();
			const QString requestId = content.value(QStringLiteral("request_id")).toString();
			bool verified = false;
			if (sender == FUserId && !deviceId.isEmpty())
				runDatabase([&](MatrixDatabase &database) {
					verified = database.isDeviceVerified(sender, deviceId);
				});
			const QByteArray secret = FSsssSecrets.value(secretName);
			if (action == QStringLiteral("request") && verified && !secret.isEmpty() &&
				!requestId.isEmpty()) {
				const QJsonObject responseContent{{QStringLiteral("request_id"), requestId},
					{QStringLiteral("secret"), QString::fromUtf8(secret)}};
				QTimer::singleShot(QRandomGenerator::global()->bounded(3000), this,
					[this, sender, deviceId, responseContent]() {
						if (!sendEncryptedToDeviceEvent(sender, deviceId,
							QStringLiteral("m.secret.send"), responseContent))
							qWarning() << "[Matrix-E2EE] failed to send verified SSSS secret response";
					});
			}
			continue;
		}
		if (toDeviceType == QStringLiteral("m.room.encrypted")) {
			if (content.value(QStringLiteral("algorithm")).toString() !=
				QStringLiteral("m.olm.v1.curve25519-aes-sha2")) {
				qWarning() << "[Matrix-E2EE] safely isolating unsupported encrypted to-device algorithm";
				continue;
			}
			const QString sender = toDeviceEvent.value(QStringLiteral("sender")).toString();
			const QString senderKey = content.value(QStringLiteral("sender_key")).toString();
			const QJsonObject ciphertexts = content.value(QStringLiteral("ciphertext")).toObject();
			qWarning() << "[Matrix-E2EE] received Olm to-device event:" << sender
				<< "ciphertext_entries:" << ciphertexts.size();
			QJsonParseError identityError;
			const QJsonObject identity = QJsonDocument::fromJson(FOlmCrypto.identityKeysJson(),
				&identityError).object();
			const QString ownCurveKey = identity.value(QStringLiteral("curve25519")).toString();
			if (!ownCurveKey.isEmpty() && senderKey == ownCurveKey) {
				qWarning() << "[Matrix-E2EE] ignoring Olm event from the local Curve25519 identity";
				continue;
			}
			const QJsonObject ciphertext = ciphertexts.value(ownCurveKey).toObject();
			const int messageType = ciphertext.value(QStringLiteral("type")).toInt(-1);
			const QByteArray body = ciphertext.value(QStringLiteral("body")).toString().toUtf8();
			QByteArray plaintext;
			QString olmSessionId;
			QString decryptedSenderDeviceId;
			QMap<QString, QByteArray> senderDevices;
			runDatabase([&](MatrixDatabase &database) {
				senderDevices = database.loadDeviceKeysForUser(sender);
			});
			for (auto deviceIt = senderDevices.constBegin(); deviceIt != senderDevices.constEnd(); ++deviceIt) {
				const QJsonObject deviceKeys = QJsonDocument::fromJson(deviceIt.value()).object()
					.value(QStringLiteral("keys")).toObject();
				const QString curveKey = QStringLiteral("curve25519:%1").arg(deviceIt.key());
				const QString legacyCurveKey = QStringLiteral("curve25519:%1:%2").arg(sender, deviceIt.key());
				if ((deviceKeys.value(curveKey).toString() == senderKey ||
					deviceKeys.value(legacyCurveKey).toString() == senderKey)) {
					decryptedSenderDeviceId = deviceIt.key();
					break;
				}
			}
			qWarning() << "[Matrix-E2EE] Olm sender-key mapping:" << sender
				<< "cached_devices:" << senderDevices.size()
				<< "resolved_device:" << decryptedSenderDeviceId
				<< "sender_key_hash:" << QCryptographicHash::hash(senderKey.toUtf8(),
					QCryptographicHash::Sha256).toHex();
			if (decryptedSenderDeviceId.isEmpty() && messageType != 0) {
				qWarning() << "[Matrix-E2EE] cannot decrypt existing Olm session without sender device mapping:"
					<< sender;
				queryDeviceKeys(sender);
				continue;
			}
			if (decryptedSenderDeviceId.isEmpty())
				qWarning() << "[Matrix-E2EE] accepting unmapped Olm pre-key message for inbound session:"
					<< sender;
			if (!sender.isEmpty() && !senderKey.isEmpty() && messageType >= 0 && !body.isEmpty())
				runDatabase([&](MatrixDatabase &database) {
					plaintext = FOlmCrypto.decryptOlm(database, sender, senderKey,
						messageType, body, olmSessionId, decryptedSenderDeviceId);
				});
			QJsonParseError decryptError;
			const QJsonDocument decryptedDocument = QJsonDocument::fromJson(plaintext, &decryptError);
			if (decryptError.error != QJsonParseError::NoError || !decryptedDocument.isObject()) {
				if ((messageType == 0 || messageType == 1) &&
					!decryptedSenderDeviceId.isEmpty()) {
					const QString recoveryKey = sender + QLatin1Char('\n') + decryptedSenderDeviceId;
					if (!FPendingOlmRecoveryDevices.contains(recoveryKey)) {
						FPendingOlmRecoveryDevices.insert(recoveryKey);
						sendEncryptedToDeviceEvent(sender, decryptedSenderDeviceId,
							QStringLiteral("m.dummy"), QJsonObject(), true);
					}
				}
				qWarning() << "[Matrix-E2EE] Olm to-device decrypt failed for sender:" << sender
					<< "error:" << decryptError.errorString();
				continue;
			}
			const QJsonObject decryptedEvent = decryptedDocument.object();
			const QJsonObject recipientKeys = decryptedEvent.value(QStringLiteral("recipient_keys")).toObject();
			const QString localEd25519 = identity.value(QStringLiteral("ed25519")).toString();
			if (decryptedEvent.value(QStringLiteral("recipient")).toString() != FUserId ||
				localEd25519.isEmpty() ||
				recipientKeys.value(QStringLiteral("ed25519")).toString() != localEd25519) {
				qWarning() << "[Matrix-E2EE] rejected Olm payload for another recipient:" << sender;
				continue;
			}
			const QString senderEd25519 = decryptedEvent.value(QStringLiteral("keys"))
				.toObject().value(QStringLiteral("ed25519")).toString();
			decryptedSenderEd25519 = senderEd25519;
			bool senderDeviceKnown = false;
			for (auto deviceIt = senderDevices.constBegin(); deviceIt != senderDevices.constEnd(); ++deviceIt) {
				const QJsonObject deviceKeys = QJsonDocument::fromJson(deviceIt.value()).object()
					.value(QStringLiteral("keys")).toObject();
				const QString curveKey = QStringLiteral("curve25519:%1").arg(deviceIt.key());
				const QString ed25519Key = QStringLiteral("ed25519:%1").arg(deviceIt.key());
				const QString legacyCurveKey = QStringLiteral("curve25519:%1:%2").arg(sender, deviceIt.key());
				const QString legacyEd25519Key = QStringLiteral("ed25519:%1:%2").arg(sender, deviceIt.key());
				if ((deviceKeys.value(curveKey).toString() == senderKey ||
					deviceKeys.value(legacyCurveKey).toString() == senderKey) &&
					(deviceKeys.value(ed25519Key).toString() == senderEd25519 ||
					deviceKeys.value(legacyEd25519Key).toString() == senderEd25519)) {
					senderDeviceKnown = true;
					decryptedSenderDeviceId = deviceIt.key();
					break;
				}
			}
			if (!senderDeviceKnown) {
				if (sender != FUserId) {
					qWarning() << "[Matrix-E2EE] rejected Olm payload from unknown sender device:" << sender;
					queryDeviceKeys(sender);
					continue;
				}
				qWarning() << "[Matrix-E2EE] accepting historical own-device Olm payload without current device mapping";
			}
			toDeviceType = decryptedEvent.value(QStringLiteral("type")).toString();
			content = decryptedEvent.value(QStringLiteral("content")).toObject();
			if (toDeviceType.isEmpty() || content.isEmpty()) {
				qWarning() << "[Matrix-E2EE] invalid decrypted to-device payload";
				continue;
			}
			if (!decryptedSenderDeviceId.isEmpty())
				FPendingOlmRecoveryDevices.remove(toDeviceSender + QLatin1Char('\n') + decryptedSenderDeviceId);
		}
		if (toDeviceType.startsWith(QStringLiteral("m.key.verification."))) {
			const QString transactionId = content.value(QStringLiteral("transaction_id")).toString();
			const QString sender = toDeviceEvent.value(QStringLiteral("sender")).toString();
			const QString eventDeviceId = content.value(QStringLiteral("from_device")).toString();
			const QString deviceId = eventDeviceId.isEmpty()
				? FSasPeerDevices.value(transactionId) : eventDeviceId;
			qWarning() << "[Matrix-E2EE] verification event:" << toDeviceType
				<< "sender:" << sender << "device:" << deviceId << "transaction:" << transactionId;
			QString nextState;
			if (toDeviceType == QStringLiteral("m.key.verification.request")) {
				qint64 timestamp = content.value(QStringLiteral("timestamp")).toVariant().toLongLong();
				// Some Matrix clients still serialize the request timestamp in
				// Unix seconds although the current API uses milliseconds.
				if (timestamp > 0 && timestamp < 100000000000LL)
					timestamp *= 1000;
				const qint64 now = QDateTime::currentMSecsSinceEpoch();
				const bool timestampValid = timestamp > 0 && timestamp <= now + 5 * 60 * 1000 &&
					timestamp >= now - 10 * 60 * 1000;
				const bool sasSupported = content.value(QStringLiteral("methods")).toArray()
					.contains(QJsonValue(QStringLiteral("m.sas.v1")));
				if (!timestampValid || !sasSupported) {
					qWarning() << "[Matrix-E2EE] ignored invalid verification request from:"
						<< sender << deviceId << "timestampValid:" << timestampValid
						<< "sasSupported:" << sasSupported;
					continue;
				}
				nextState = QStringLiteral("requested");
				FSasInitiatorUsers.insert(transactionId, sender);
				FSasPeerDevices.insert(transactionId, eventDeviceId);
				if (!eventDeviceId.isEmpty()) {
					FDeviceKeyQueries.remove(sender);
					FDeviceKeyQueries.insert(sender);
					queryDeviceKeys(sender, eventDeviceId);
				}
				emit verificationRequestReceived(transactionId, sender, deviceId);
			}
			else if (toDeviceType == QStringLiteral("m.key.verification.ready") &&
				FSasStates.value(transactionId) == QStringLiteral("requested"))
				nextState = QStringLiteral("ready");
			else if (toDeviceType == QStringLiteral("m.key.verification.start") &&
				FSasStates.value(transactionId) == QStringLiteral("ready")) {
				const auto contains = [&content](const QString &name, const QString &value) {
					return content.value(name).toArray().contains(QJsonValue(value));
				};
				const bool supportedStart = content.value(QStringLiteral("method")).toString() == QStringLiteral("m.sas.v1") &&
					contains(QStringLiteral("key_agreement_protocols"), QStringLiteral("curve25519-hkdf-sha256")) &&
					contains(QStringLiteral("hashes"), QStringLiteral("sha256")) &&
					(content.value(QStringLiteral("message_authentication_codes")).toArray().contains(
						QJsonValue(QStringLiteral("hkdf-hmac-sha256.v2"))) ||
					 content.value(QStringLiteral("message_authentication_codes")).toArray().contains(
						QJsonValue(QStringLiteral("hkdf-hmac-sha256")))) &&
					contains(QStringLiteral("short_authentication_string"), QStringLiteral("emoji")) &&
					contains(QStringLiteral("short_authentication_string"), QStringLiteral("decimal"));
				if (!supportedStart) {
					const QJsonObject cancel{{QStringLiteral("transaction_id"), transactionId},
						{QStringLiteral("from_device"), FDeviceId},
						{QStringLiteral("code"), QStringLiteral("m.unknown_method")}};
					sendVerificationEvent(QStringLiteral("m.key.verification.cancel"), transactionId,
						sender, deviceId, cancel);
					qWarning() << "[Matrix-E2EE] rejected unsupported SAS start:" << transactionId;
					continue;
				}
				QByteArray publicKey;
				if (FOlmCrypto.createSas(transactionId, publicKey)) {
					FOlmCrypto.setSasMacMethod(transactionId, QStringLiteral("hkdf-hmac-sha256"));
					FSasPublicKeys.insert(transactionId, publicKey);
					FSasInitiatorUsers.insert(transactionId, sender);
					FSasInitiatorDevices.insert(transactionId, deviceId);
					FSasPeerDevices.insert(transactionId, deviceId);
					const QJsonArray offeredMacMethods = content.value(
						QStringLiteral("message_authentication_codes")).toArray();
					const QString selectedMacMethod = offeredMacMethods.contains(
						QJsonValue(QStringLiteral("hkdf-hmac-sha256.v2")))
						? QStringLiteral("hkdf-hmac-sha256.v2")
						: QStringLiteral("hkdf-hmac-sha256");
					FOlmCrypto.setSasMacMethod(transactionId, selectedMacMethod);
					qWarning() << "[Matrix-E2EE] SAS MAC method selected:" << transactionId
						<< selectedMacMethod;
					const QByteArray commitment = FOlmCrypto.sasCommitment(publicKey, content);
					const QJsonObject accept{{QStringLiteral("transaction_id"), transactionId},
						{QStringLiteral("method"), QStringLiteral("m.sas.v1")},
						{QStringLiteral("key_agreement_protocol"), QStringLiteral("curve25519-hkdf-sha256")},
						{QStringLiteral("hash"), QStringLiteral("sha256")},
						{QStringLiteral("message_authentication_code"), selectedMacMethod},
						{QStringLiteral("short_authentication_string"), QJsonArray{QStringLiteral("decimal"), QStringLiteral("emoji")}},
						{QStringLiteral("commitment"), QString::fromLatin1(commitment)}};
					if (sendVerificationEvent(QStringLiteral("m.key.verification.accept"), transactionId,
						sender, deviceId, accept)) {
						FSasStartContents.insert(transactionId, content);
						FSasCommitments.insert(transactionId, commitment);
						nextState = QStringLiteral("accepted");
					}
				}
			}
			else if (toDeviceType == QStringLiteral("m.key.verification.accept") &&
				FSasStates.value(transactionId) == QStringLiteral("started")) {
				const QByteArray publicKey = FSasPublicKeys.value(transactionId);
				FSasCommitments.insert(transactionId, content.value(QStringLiteral("commitment")).toString().toLatin1());
				FOlmCrypto.setSasMacMethod(transactionId,
					content.value(QStringLiteral("message_authentication_code")).toString());
				const QJsonObject key{{QStringLiteral("transaction_id"), transactionId},
					{QStringLiteral("key"), QString::fromLatin1(publicKey)}};
				if (!publicKey.isEmpty() && sendVerificationEvent(QStringLiteral("m.key.verification.key"),
					transactionId, sender, deviceId, key))
					nextState = QStringLiteral("key_sent");
			}
			else if (toDeviceType == QStringLiteral("m.key.verification.key") &&
				(FSasStates.value(transactionId) == QStringLiteral("accepted") ||
				 FSasStates.value(transactionId) == QStringLiteral("key_sent"))) {
				const QByteArray theirKey = content.value(QStringLiteral("key")).toString().toLatin1();
				const bool sasKeyAccepted = FOlmCrypto.setSasTheirKey(transactionId, theirKey);
				qWarning() << "[Matrix-E2EE] SAS peer key received:" << transactionId
					<< "localInitiatorCandidate:" << (FSasInitiatorUsers.value(transactionId) == FUserId)
					<< "keyAccepted:" << sasKeyAccepted;
				if (sasKeyAccepted) {
					const QString initiatorUser = FSasInitiatorUsers.value(transactionId);
					const bool localInitiator = initiatorUser == FUserId &&
						FSasInitiatorDevices.value(transactionId) == FDeviceId;
					if (localInitiator) {
						const QByteArray expectedCommitment = FOlmCrypto.sasCommitment(theirKey,
							FSasStartContents.value(transactionId));
						qWarning() << "[Matrix-E2EE] SAS commitment check:" << transactionId
							<< "receivedLength:" << FSasCommitments.value(transactionId).size()
							<< "expectedLength:" << expectedCommitment.size()
							<< "receivedHash:" << QCryptographicHash::hash(
								FSasCommitments.value(transactionId), QCryptographicHash::Sha256).toHex()
							<< "expectedHash:" << QCryptographicHash::hash(
								expectedCommitment, QCryptographicHash::Sha256).toHex()
							<< "peerKeyLength:" << theirKey.size();
						if (expectedCommitment.isEmpty() || expectedCommitment != FSasCommitments.value(transactionId)) {
							const QJsonObject cancel{{QStringLiteral("transaction_id"), transactionId},
								{QStringLiteral("from_device"), FDeviceId},
								{QStringLiteral("code"), QStringLiteral("m.mismatched_commitment")}};
							sendVerificationEvent(QStringLiteral("m.key.verification.cancel"), transactionId,
								sender, deviceId, cancel);
							qWarning() << "[Matrix-E2EE] rejected mismatched SAS commitment:" << transactionId;
							nextState = QStringLiteral("cancelled");
							continue;
						}
					}
					FSasTheirPublicKeys.insert(transactionId, theirKey);
					nextState = QStringLiteral("key_received");
					const QString initiatorDevice = FSasInitiatorDevices.value(transactionId);
					const QString receiverUser = localInitiator ? sender : FUserId;
					const QString receiverDevice = localInitiator ? deviceId : FDeviceId;
					if (!localInitiator) {
						const QByteArray responderKey = FSasPublicKeys.value(transactionId);
						const QJsonObject key{{QStringLiteral("transaction_id"), transactionId},
							{QStringLiteral("key"), QString::fromLatin1(responderKey)}};
						if (!responderKey.isEmpty() && !sendVerificationEvent(
							QStringLiteral("m.key.verification.key"), transactionId,
							sender, deviceId, key))
							qWarning() << "[Matrix-E2EE] failed to send responder SAS key:" << transactionId;
					}
					const QByteArray localPublicKey = FSasPublicKeys.value(transactionId);
					const QByteArray sasInfo = localInitiator
						? QStringLiteral("MATRIX_KEY_VERIFICATION_SAS|%1|%2|%3|%4|%5|%6|%7")
							.arg(FUserId, FDeviceId, QString::fromLatin1(localPublicKey),
								sender, deviceId, QString::fromLatin1(theirKey), transactionId).toUtf8()
						: QStringLiteral("MATRIX_KEY_VERIFICATION_SAS|%1|%2|%3|%4|%5|%6|%7")
							.arg(initiatorUser, initiatorDevice, QString::fromLatin1(theirKey),
								FUserId, FDeviceId, QString::fromLatin1(localPublicKey), transactionId).toUtf8();
					const QByteArray sasBytes = FOlmCrypto.generateSasBytes(transactionId, sasInfo, 6);
					qWarning() << "[Matrix-E2EE] SAS bytes generated:" << transactionId
						<< "size:" << sasBytes.size()
						<< "sasInfoHash:" << QCryptographicHash::hash(sasInfo,
							QCryptographicHash::Sha256).toHex()
						<< "sasBytesHash:" << QCryptographicHash::hash(sasBytes,
							QCryptographicHash::Sha256).toHex();
					if (sasBytes.size() == 6)
						emit verificationSasAvailable(transactionId, FOlmCrypto.sasEmoji(sasBytes),
							FOlmCrypto.sasDecimal(sasBytes));
				}
			}
			else if (toDeviceType == QStringLiteral("m.key.verification.mac") &&
				FSasStates.value(transactionId) == QStringLiteral("key_received")) {
				QByteArray deviceKeysJson;
				const QJsonObject macs = content.value(QStringLiteral("mac")).toObject();
				bool deviceKeysLoaded = false;
				runDatabase([&](MatrixDatabase &database) {
					deviceKeysLoaded = database.loadDeviceKeys(sender, deviceId, deviceKeysJson);
				});
				QJsonObject queriedKeys = FVerificationKeyQueryResponses.value(
					sender + QLatin1Char('\n') + deviceId);
				if (queriedKeys.isEmpty())
					queriedKeys = FVerificationKeyQueryResponses.value(sender + QLatin1Char('\n'));
				QJsonObject remoteKeys = queriedKeys.value(QStringLiteral("device_keys"))
					.toObject().value(sender).toObject().value(deviceId).toObject()
					.value(QStringLiteral("keys")).toObject();
				if (remoteKeys.isEmpty())
					remoteKeys = deviceKeysLoaded ? QJsonDocument::fromJson(deviceKeysJson).object()
						.value(QStringLiteral("keys")).toObject() : QJsonObject();
				const QJsonObject queriedMaster = queriedKeys.value(QStringLiteral("master_keys"))
					.toObject().value(sender).toObject();
				const QJsonObject queriedSelfSigning = queriedKeys.value(QStringLiteral("self_signing_keys"))
					.toObject().value(sender).toObject();
				const QJsonObject queriedUserSigning = queriedKeys.value(QStringLiteral("user_signing_keys"))
					.toObject().value(sender).toObject();
				auto addQueriedCrossSigningKeys = [&remoteKeys](const QJsonObject &crossSigningKey) {
					const QJsonObject keys = crossSigningKey.value(QStringLiteral("keys")).toObject();
					for (auto keyIt = keys.constBegin(); keyIt != keys.constEnd(); ++keyIt)
						remoteKeys.insert(keyIt.key(), keyIt.value());
				};
				addQueriedCrossSigningKeys(queriedMaster);
				addQueriedCrossSigningKeys(queriedSelfSigning);
				addQueriedCrossSigningKeys(queriedUserSigning);
				for (const QString &crossSigningType : {QStringLiteral("master"),
					QStringLiteral("self_signing"), QStringLiteral("user_signing")}) {
					QByteArray crossSigningJson;
					bool crossSigningLoaded = false;
					runDatabase([&](MatrixDatabase &database) {
						crossSigningLoaded = database.loadCrossSigningKey(sender,
							crossSigningType, crossSigningJson);
					});
					if (!crossSigningLoaded)
						continue;
					const QJsonObject crossSigningKeys = QJsonDocument::fromJson(crossSigningJson)
						.object().value(QStringLiteral("keys")).toObject();
					for (auto keyIt = crossSigningKeys.constBegin(); keyIt != crossSigningKeys.constEnd(); ++keyIt)
						remoteKeys.insert(keyIt.key(), keyIt.value());
				}
				const QByteArray macInfo = QStringLiteral("MATRIX_KEY_VERIFICATION_MAC%1%2%3%4%5")
					.arg(sender, deviceId, FUserId, FDeviceId, transactionId).toUtf8();
				qWarning() << "[Matrix-E2EE] SAS MAC info role order:"
					<< "macSender:" << sender << deviceId
					<< "macReceiver:" << FUserId << FDeviceId;
				QStringList receivedMacKeys;
				for (auto macIt = macs.constBegin(); macIt != macs.constEnd(); ++macIt)
					if (macIt.key() != QStringLiteral("keys"))
						receivedMacKeys.append(macIt.key());
				qWarning() << "[Matrix-E2EE] SAS MAC key sets:"
					<< "received:" << receivedMacKeys
					<< "remote:" << remoteKeys.keys()
					<< "remoteKeysLoaded:" << deviceKeysLoaded;
				bool valid = !macs.isEmpty() && !remoteKeys.isEmpty();
				bool missingKey = false;
				QStringList keyIds;
				for (auto macIt = macs.constBegin(); macIt != macs.constEnd(); ++macIt) {
					if (macIt.key() == QStringLiteral("keys"))
						continue;
					if (!macIt.key().startsWith(QStringLiteral("ed25519:")) ||
						!remoteKeys.contains(macIt.key())) {
						qWarning() << "[Matrix-E2EE] SAS MAC key unavailable:" << macIt.key();
						missingKey = true;
						valid = false;
						continue;
					}
					keyIds.append(macIt.key());
					const QByteArray keyInfo = macInfo + macIt.key().toUtf8();
					const bool keyMacValid = FOlmCrypto.verifySasMac(transactionId,
						remoteKeys.value(macIt.key()).toString().toUtf8(),
						keyInfo,
						macIt.value().toString().toLatin1());
					const QByteArray reversedRoleInfo = QStringLiteral("MATRIX_KEY_VERIFICATION_MAC%1%2%3%4%5")
						.arg(FUserId, FDeviceId, sender, deviceId, transactionId).toUtf8() + macIt.key().toUtf8();
					const bool reversedRoleMac = FOlmCrypto.verifySasMac(transactionId,
						remoteKeys.value(macIt.key()).toString().toUtf8(), reversedRoleInfo,
						macIt.value().toString().toLatin1());
					const bool keyBeforeInfoMac = FOlmCrypto.verifySasMac(transactionId,
						remoteKeys.value(macIt.key()).toString().toUtf8(),
						macIt.key().toUtf8() + macInfo, macIt.value().toString().toLatin1());
					const QByteArray remoteKey = remoteKeys.value(macIt.key()).toString().toUtf8();
					const QByteArray receivedMac = macIt.value().toString().toLatin1();
					const QByteArray calculatedMac = FOlmCrypto.calculateSasMac(transactionId,
						remoteKey, keyInfo);
					qWarning() << "[Matrix-E2EE] SAS MAC key check:" << macIt.key()
						<< "valid:" << keyMacValid
						<< "reversedRoleCandidate:" << reversedRoleMac
						<< "keyBeforeInfoCandidate:" << keyBeforeInfoMac
						<< "remoteKeyHash:" << QCryptographicHash::hash(remoteKey,
							QCryptographicHash::Sha256).toHex()
						<< "receivedMacHash:" << QCryptographicHash::hash(receivedMac,
							QCryptographicHash::Sha256).toHex()
						<< "calculatedMacHash:" << QCryptographicHash::hash(calculatedMac,
							QCryptographicHash::Sha256).toHex();
					if (!keyMacValid)
						valid = false;
				}
				if (missingKey) {
					FPendingVerificationMacs.insert(transactionId, toDeviceEvent);
					queryDeviceKeys(sender, deviceId);
					qWarning() << "[Matrix-E2EE] deferred SAS MAC until device keys are loaded:"
						<< transactionId;
					continue;
				}
				std::sort(keyIds.begin(), keyIds.end());
				const QByteArray expectedKeysMac = content.value(QStringLiteral("keys")).toString().toLatin1();
				const bool keysMacValid = !keyIds.isEmpty() && FOlmCrypto.verifySasMac(transactionId,
					keyIds.join(',').toUtf8(), macInfo + QByteArrayLiteral("KEY_IDS"), expectedKeysMac);
				qWarning() << "[Matrix-E2EE] SAS keys MAC check:" << keysMacValid;
				if (!keysMacValid)
					valid = false;
				if (valid) {
					nextState = QStringLiteral("mac_verified");
					if (FSasStates.value(transactionId) == QStringLiteral("mac_sent")) {
						const QJsonObject done{{QStringLiteral("transaction_id"), transactionId},
							{QStringLiteral("from_device"), FDeviceId}};
						if (sendVerificationEvent(QStringLiteral("m.key.verification.done"), transactionId,
							sender, deviceId, done))
							nextState = QStringLiteral("done_sent");
					}
				} else
					qWarning() << "Rejected invalid Matrix SAS MAC" << transactionId << sender << deviceId;
			}
			else if (toDeviceType == QStringLiteral("m.key.verification.done") &&
				(FSasStates.value(transactionId) == QStringLiteral("done_sent") ||
				 FSasStates.value(transactionId) == QStringLiteral("mac_sent"))) {
				const bool replyWithDone = FSasStates.value(transactionId) == QStringLiteral("mac_sent");
				bool doneSent = !replyWithDone;
				if (replyWithDone) {
					const QJsonObject done{{QStringLiteral("transaction_id"), transactionId},
						{QStringLiteral("from_device"), FDeviceId}};
					doneSent = sendVerificationEvent(QStringLiteral("m.key.verification.done"), transactionId,
						sender, deviceId, done);
					if (doneSent)
						qWarning() << "[Matrix-E2EE] SAS done sent after peer done:" << transactionId;
				}
				if (doneSent) {
					QByteArray deviceKeysJson;
					bool deviceKeysLoaded = false;
					runDatabase([&](MatrixDatabase &database) {
						deviceKeysLoaded = database.loadDeviceKeys(sender, deviceId, deviceKeysJson);
					});
					const QJsonObject remoteKeys = deviceKeysLoaded ? QJsonDocument::fromJson(deviceKeysJson).object()
						.value(QStringLiteral("keys")).toObject() : QJsonObject();
					const QString identityKey = remoteKeys.value(QStringLiteral("ed25519:%1").arg(deviceId)).toString();
					bool trustSaved = false;
					runDatabase([&](MatrixDatabase &database) {
						trustSaved = database.saveDeviceTrust(sender, deviceId, identityKey,
							QStringLiteral("verified"));
					});
					if (!identityKey.isEmpty() && trustSaved)
						nextState = QStringLiteral("verified");
					if (sender == FUserId && !identityKey.isEmpty() && trustSaved) {
						QByteArray masterJson;
						bool masterLoaded = false;
						runDatabase([&](MatrixDatabase &database) {
							masterLoaded = database.loadCrossSigningKey(FUserId,
								QStringLiteral("master"), masterJson);
						});
						QJsonObject master = masterLoaded
							? QJsonDocument::fromJson(masterJson).object() : QJsonObject();
						if (master.isEmpty())
							master = FVerificationKeyQueryResponses.value(FUserId + QLatin1Char('\n'))
								.value(QStringLiteral("master_keys")).toObject().value(FUserId).toObject();
						if (!master.isEmpty())
							uploadOwnMasterKeySignature(master);
					}
				}
			}
			else if (toDeviceType == QStringLiteral("m.key.verification.cancel")) {
				nextState = QStringLiteral("cancelled");
				qWarning() << "[Matrix-E2EE] verification cancelled by peer:" << transactionId
					<< "code:" << content.value(QStringLiteral("code")).toString()
					<< "reason:" << content.value(QStringLiteral("reason")).toString();
			}
			if (!transactionId.isEmpty() && !sender.isEmpty() && !deviceId.isEmpty() &&
				!nextState.isEmpty()) {
				FSasStates.insert(transactionId, nextState);
				const quint64 generation = FSasTimeoutGenerations.value(transactionId) + 1;
				FSasTimeoutGenerations.insert(transactionId, generation);
				if (nextState == QStringLiteral("cancelled") ||
					nextState == QStringLiteral("done_pending_mac_verification")) {
					FSasTimeoutGenerations.remove(transactionId);
				} else {
					QTimer::singleShot(5 * 60 * 1000, this,
						[this, transactionId, sender, deviceId, generation]() {
							if (FSasTimeoutGenerations.value(transactionId) != generation)
								return;
							const QString state = FSasStates.value(transactionId);
							if (state.isEmpty() || state == QStringLiteral("cancelled") ||
								state == QStringLiteral("done_pending_mac_verification"))
								return;
							const QString timeoutState = QStringLiteral("cancelled_timeout");
							FSasStates.insert(transactionId, timeoutState);
							FSasTimeoutGenerations.remove(transactionId);
							bool timeoutSaved = false;
							runDatabase([&](MatrixDatabase &database) {
								timeoutSaved = database.saveVerificationState(transactionId, sender, deviceId,
									timeoutState, QByteArrayLiteral("{}"));
							});
							if (!timeoutSaved)
								qWarning() << "Failed to persist Matrix SAS timeout" << transactionId;
							emit verificationStateChanged(transactionId, timeoutState);
						});
				}
				bool stateSaved = false;
				runDatabase([&](MatrixDatabase &database) {
					stateSaved = database.saveVerificationState(transactionId, sender, deviceId,
						nextState, QJsonDocument(toDeviceEvent).toJson(QJsonDocument::Compact));
				});
				if (!stateSaved)
					qWarning() << "Failed to persist Matrix SAS state" << transactionId;
				emit verificationStateChanged(transactionId, nextState);
			}
			continue;
		}
		if (toDeviceType == QStringLiteral("m.room_key_request")) {
			const QString action = content.value(QStringLiteral("action")).toString();
			const QString requestId = content.value(QStringLiteral("request_id")).toString();
			if (action == QStringLiteral("request_cancellation")) {
				if (!requestId.isEmpty())
					FPendingKeyRequests.remove(requestId);
				qWarning() << "[Matrix-E2EE] cancelled pending room-key request:" << requestId;
				continue;
			}
		}
		if (toDeviceType == QStringLiteral("m.room_key_request") &&
			content.value(QStringLiteral("action")).toString() == QStringLiteral("request")) {
			const QJsonObject body = content.value(QStringLiteral("body")).toObject();
			if (body.value(QStringLiteral("algorithm")).toString() !=
				QStringLiteral("m.megolm.v1.aes-sha2"))
				continue;
			const QString requestId = content.value(QStringLiteral("request_id")).toString();
			const QString sender = toDeviceEvent.value(QStringLiteral("sender")).toString();
			const QString deviceId = content.value(QStringLiteral("requesting_device_id")).toString();
			const QString roomId = body.value(QStringLiteral("room_id")).toString();
			const QString sessionId = body.value(QStringLiteral("session_id")).toString();
			if (requestId.isEmpty() || sender.isEmpty() || deviceId.isEmpty() ||
				roomId.isEmpty() || sessionId.isEmpty())
				continue;
			const auto roomIt = FRooms.constFind(roomId);
			if (roomIt == FRooms.constEnd() || !roomIt->isJoined)
				continue;
			bool senderJoined = false;
			for (const ProtocolRosterEntry &member : roomIt->members) {
				if (member.id == sender && member.isValid) {
					senderJoined = true;
					break;
				}
			}
			if (!senderJoined)
				continue;
			QString storedRoomId;
			QString storedSenderKey;
			QByteArray storedPickle;
			bool sessionLoaded = false;
			runDatabase([&](MatrixDatabase &database) {
				sessionLoaded = database.loadMegolmSession(sessionId, storedRoomId,
					storedSenderKey, storedPickle);
			});
			if (!sessionLoaded || storedRoomId != roomId || storedSenderKey.isEmpty())
				continue;
			QJsonParseError localIdentityError;
			const QString localCurveKey = QJsonDocument::fromJson(FOlmCrypto.identityKeysJson(),
				&localIdentityError).object().value(QStringLiteral("curve25519")).toString();
			bool requesterVerified = false;
			bool previouslyShared = false;
			int previousShareIndex = -1;
			runDatabase([&](MatrixDatabase &database) {
				requesterVerified = database.isDeviceVerified(sender, deviceId);
				previouslyShared = database.wasMegolmKeyShared(sessionId, sender, deviceId);
				if (previouslyShared)
					previousShareIndex = database.megolmKeyShareIndex(sessionId, sender, deviceId);
			});
			if (sender == FUserId && !requesterVerified)
				requesterVerified = isOwnDeviceCrossSigningVerified(deviceId);
			if ((!requesterVerified && !previouslyShared) ||
				(sender != FUserId && storedSenderKey != localCurveKey)) {
				qWarning() << "[Matrix-E2EE] rejected room-key request from untrusted device or non-owner:"
					<< sender << deviceId << roomId << sessionId;
				continue;
			}
			bool requestKnown = false;
			runDatabase([&](MatrixDatabase &database) {
				requestKnown = database.hasKeyRequest(requestId);
			});
			bool requestSaved = false;
			if (!requestKnown)
				runDatabase([&](MatrixDatabase &database) {
					requestSaved = database.saveKeyRequest(requestId, sender, deviceId, roomId, sessionId);
				});
			if (!requestId.isEmpty() && !requestKnown && requestSaved) {
				FPendingKeyRequests.insert(requestId, QJsonObject{
					{QStringLiteral("sender"), sender},
					{QStringLiteral("device_id"), deviceId},
					{QStringLiteral("room_id"), roomId},
					{QStringLiteral("session_id"), sessionId}});
				emit roomKeyRequestReceived(sender, deviceId, roomId, sessionId, requestId);
				sendForwardedRoomKey(sender, deviceId, roomId, sessionId, requestId,
					static_cast<quint32>(qMax(0, previouslyShared ? previousShareIndex : 0)));
				queryDeviceKeys(sender, deviceId);
			}
			continue;
		}
		if (toDeviceType == QStringLiteral("m.forwarded_room_key")) {
			const QString forwardingUser = toDeviceEvent.value(QStringLiteral("sender")).toString();
			const QString forwardedRoomId = content.value(QStringLiteral("room_id")).toString();
			bool forwardingUserJoined = forwardingUser == FUserId;
			const auto forwardedRoomIt = FRooms.constFind(forwardedRoomId);
			if (!forwardingUserJoined && forwardedRoomIt != FRooms.constEnd()) {
				for (const ProtocolRosterEntry &member : forwardedRoomIt->members) {
					if (member.isValid && member.id == forwardingUser) {
						forwardingUserJoined = true;
						break;
					}
				}
			}
			if (forwardingUser.isEmpty() || !forwardingUserJoined) {
				qWarning() << "[Matrix-E2EE] rejected forwarded room key from non-member:"
					<< forwardingUser << forwardedRoomId;
				continue;
			}
			const QString algorithm = content.value(QStringLiteral("algorithm")).toString();
			const QString sessionId = content.value(QStringLiteral("session_id")).toString();
			const QString roomId = content.value(QStringLiteral("room_id")).toString();
			const QString sessionKey = content.value(QStringLiteral("session_key")).toString();
			const QString senderKey = content.value(QStringLiteral("sender_key")).toString();
			const QString claimedEd25519 = content.value(
				QStringLiteral("sender_claimed_ed25519_key")).toString();
			const QString forwardingChainJson = QString::fromUtf8(QJsonDocument(content.value(
				QStringLiteral("forwarding_curve25519_key_chain")).toArray()).toJson(QJsonDocument::Compact));
			if (algorithm != QStringLiteral("m.megolm.v1.aes-sha2") || sessionId.isEmpty() ||
				roomId.isEmpty() || sessionKey.isEmpty() || senderKey.isEmpty()) {
				qWarning() << "[Matrix-E2EE] rejected malformed forwarded room key from"
					<< toDeviceEvent.value(QStringLiteral("sender")).toString();
				continue;
			}
			QByteArray sessionPickle;
			const bool sessionPrepared = FOlmCrypto.prepareExportedMegolmSession(sessionId,
				sessionKey.toUtf8(), sessionPickle);
			bool sessionSaved = false;
			if (sessionPrepared)
				runDatabase([&](MatrixDatabase &database) {
					sessionSaved = database.saveMegolmSession(sessionId, roomId, senderKey,
						sessionPickle, claimedEd25519, forwardingChainJson);
				});
			if (sessionPrepared && sessionSaved) {
					retryPendingEncryptedEvents(roomId, sessionId);
				cancelMissingRoomKeyRequest(roomId, senderKey, sessionId);
			} else
				qWarning() << "[Matrix-E2EE] rejected forwarded room key session" << sessionId;
			continue;
		}
		if (toDeviceType == QStringLiteral("m.secret.send")) {
			const QString requestId = content.value(QStringLiteral("request_id")).toString();
			const QString secret = content.value(QStringLiteral("secret")).toString();
			bool senderVerified = false;
			if (toDeviceSender == FUserId && !decryptedSenderDeviceId.isEmpty())
				runDatabase([&](MatrixDatabase &database) {
					senderVerified = database.isDeviceVerified(FUserId, decryptedSenderDeviceId);
				});
			QString secretName;
			for (auto it = FSsssSecretRequestIds.constBegin(); it != FSsssSecretRequestIds.constEnd(); ++it) {
				if (it.value() == requestId) {
					secretName = it.key();
					break;
				}
			}
			if (senderVerified && !secretName.isEmpty() && !secret.isEmpty()) {
				FSsssSecrets.insert(secretName, secret.toUtf8());
				FSsssSecretRequestIds.remove(secretName);
				FSsssPendingSecrets.remove(secretName);
				if (!requestId.isEmpty()) {
					QMap<QString, QByteArray> ownDevices;
					runDatabase([&](MatrixDatabase &database) {
						ownDevices = database.loadDeviceKeysForUser(FUserId);
					});
					const QJsonObject cancellation{{QStringLiteral("action"), QStringLiteral("request_cancellation")},
						{QStringLiteral("request_id"), requestId},
						{QStringLiteral("requesting_device_id"), FDeviceId}};
					for (auto deviceIt = ownDevices.constBegin(); deviceIt != ownDevices.constEnd(); ++deviceIt) {
						if (deviceIt.key() == FDeviceId || deviceIt.key() == decryptedSenderDeviceId)
							continue;
						bool verified = false;
						runDatabase([&](MatrixDatabase &database) {
							verified = database.isDeviceVerified(FUserId, deviceIt.key());
						});
						if (verified) {
							const QJsonObject messages{{FUserId, QJsonObject{{deviceIt.key(), cancellation}}}};
							QNetworkRequest cancellationRequest(QUrl(constructUrl(
								QStringLiteral("/_matrix/client/v3/sendToDevice/m.secret.request/%1")
									.arg(QUrl::toPercentEncoding(generateTransactionId())))));
							cancellationRequest.setAttribute(QNetworkRequest::Http2AllowedAttribute, FUseHttp2);
							cancellationRequest.setRawHeader("Authorization", QByteArray("Bearer ") + FAccesToken.toUtf8());
							cancellationRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
							QNetworkReply *cancellationReply = FNetworkAccessManager->put(cancellationRequest,
								QJsonDocument(QJsonObject{{QStringLiteral("messages"), messages}})
									.toJson(QJsonDocument::Compact));
							cancellationReply->setProperty("requestType", QStringLiteral("to_device"));
							cancellationReply->setProperty("e2eeEventType", QStringLiteral("m.secret.request"));
						}
					}
				}
				if (FSsssPendingSecrets.isEmpty()) {
					if (!validateRecoveredMasterSecret()) {
						FSsssSecrets.clear();
						emit ssssRecoveryFinished(false, QStringLiteral("SSSS master key does not match the server master key"));
						return;
					}
					uploadRecoveredMasterKeySignature();
					uploadOwnDeviceSignature(FSsssSecrets.value(QStringLiteral("m.cross_signing.self_signing")));
					FSsssRecoveryInput.clear();
					FSsssKeyId.clear();
					FSsssKeyDescription = QJsonObject();
					FSsssDerivedKey.clear();
					emit ssssRecoveryFinished(true, QString());
				}
			}
			continue;
		}
		if (toDeviceType.startsWith(QStringLiteral("m.cross_signing.")) ||
			toDeviceType == QStringLiteral("m.room.encrypted")) {
			qWarning() << "[Matrix-E2EE] safely isolating unsupported encrypted to-device event:"
				<< toDeviceType;
			continue;
		}
		if (toDeviceType != QStringLiteral("m.room_key"))
			continue;
		if (content.value(QStringLiteral("algorithm")).toString() !=
			QStringLiteral("m.megolm.v1.aes-sha2"))
			continue;
		const QString sessionId = content.value(QStringLiteral("session_id")).toString();
		const QString roomId = content.value(QStringLiteral("room_id")).toString();
		const QString senderKey = content.value(QStringLiteral("sender_key")).toString();
		const QString sessionKey = content.value(QStringLiteral("session_key")).toString();
		const QString senderClaimedEd25519 = decryptedSenderEd25519;
		if (sessionId.isEmpty() || roomId.isEmpty() || senderKey.isEmpty() || sessionKey.isEmpty())
			continue;
		QByteArray sessionPickle;
		const bool sessionPrepared = FOlmCrypto.prepareMegolmSession(sessionId,
			sessionKey.toUtf8(), sessionPickle);
		bool sessionSaved = false;
		if (sessionPrepared)
			runDatabase([&](MatrixDatabase &database) {
				sessionSaved = database.saveMegolmSession(sessionId, roomId, senderKey, sessionPickle,
				senderClaimedEd25519,
				QString::fromUtf8(QJsonDocument(QJsonArray{senderKey}).toJson(QJsonDocument::Compact)));
			});
		if (sessionPrepared && sessionSaved) {
			retryPendingEncryptedEvents(roomId, sessionId);
			cancelMissingRoomKeyRequest(roomId, senderKey, sessionId);
		}
	}


	// Parse presence from top-level presence.events once (outside room loop)
	const QJsonObject presence = obj.value("presence").toObject();
	if (!presence.isEmpty()) {
		const QJsonArray presenceEvents = presence.value("events").toArray();
		for (const QJsonValue &presenceVal : presenceEvents) {
			const QJsonObject presenceEvent = presenceVal.toObject();
			QString type = presenceEvent.value("type").toString();

			if (type == "m.presence") {
				// Parse Matrix presence event
				const QJsonObject content = presenceEvent.value("content").toObject();
				QString userId = presenceEvent.value("sender").toString();
				QString presenceType = content.value("presence").toString();
				QString statusText = content.value("status_msg").toString();
				qint64 lastActiveTs = content.value("last_active_ts").toVariant().toLongLong();

				// Convert Matrix presence to ProtocolPresence
				ProtocolPresenceUpdate update;
				update.accountId = userId;
				update.userId = userId;
				update.presence = (presenceType == "online") ? PresenceOnline :
				                  (presenceType == "offline") ? PresenceOffline :
				                  (presenceType == "unavailable") ? PresenceAway :
				                  PresenceUnknown;
				update.status = statusText;
				update.lastActiveAgo = lastActiveTs > 0 ?
				                     QDateTime::currentMSecsSinceEpoch() - lastActiveTs :
				                     0;
				update.currentlyActive = (presenceType == "online");

				// Keep the initial sync database-only; publish presence afterwards.
				if (!wasInitialSync)
					emit presenceReceived(update);
			}
		}
	}

	QMap<QString, QList<MatrixTimelineEvent>> persistentEvents;
	bool syncMetadataStarted = false;
	runDatabase([&](MatrixDatabase &database) {
		syncMetadataStarted = database.beginSyncMetadataBatch();
	});
	if (!syncMetadataStarted)
		qWarning() << "Failed to begin Matrix sync metadata transaction";
	const auto databaseBool = [this](const std::function<bool(MatrixDatabase &)> &operation) {
		bool result = false;
		runDatabase([&](MatrixDatabase &database) { result = operation(database); });
		return result;
	};
	QList<QString> joinedRoomIds;
	joinedRoomIds.reserve(joined.size());
	if (!FActiveRoomId.isEmpty() && joined.contains(FActiveRoomId))
		joinedRoomIds.append(FActiveRoomId);
	for (auto roomIt = joined.constBegin(); roomIt != joined.constEnd(); ++roomIt)
		if (roomIt.key() != FActiveRoomId)
			joinedRoomIds.append(roomIt.key());
	for (const QString &roomId : joinedRoomIds) {
		const QJsonObject roomData = joined.value(roomId).toObject();
		if (!FRoomPrevBatch.contains(roomId)) {
			QString storedPrevBatch;
			bool storedLimited = false;
			bool boundaryLoaded = false;
			runDatabase([&](MatrixDatabase &database) {
				boundaryLoaded = database.loadRoomTimelineBoundary(roomId, storedPrevBatch, storedLimited);
			});
			if (boundaryLoaded && !storedPrevBatch.isEmpty()) {
				FRoomPrevBatch.insert(roomId, storedPrevBatch);
				if (storedLimited)
					FLimitedRooms.insert(roomId);
			}
		}
		if (FReplacedRoomIds.contains(roomId))
			continue;
		const auto tombstoneReplacement = [](const QJsonValue &eventValue) {
			const QJsonObject event = eventValue.toObject();
			if (event.value(QStringLiteral("type")).toString() != QStringLiteral("m.room.tombstone") ||
				event.value(QStringLiteral("state_key")).toString() != QString())
				return QString();
			return event.value(QStringLiteral("content")).toObject()
				.value(QStringLiteral("replacement_room")).toString();
		};
		QString replacementRoom;
		const QJsonArray tombstoneStateEvents = roomData.value(QStringLiteral("state")).toObject()
			.value(QStringLiteral("events")).toArray();
		const QJsonArray tombstoneStateAfterEvents = roomData.value(QStringLiteral("state_after"))
			.toObject().value(QStringLiteral("events")).toArray();
		for (const QJsonValue &stateVal : tombstoneStateEvents) {
			replacementRoom = tombstoneReplacement(stateVal);
			if (!replacementRoom.isEmpty())
				break;
		}
		if (replacementRoom.isEmpty())
			for (const QJsonValue &stateVal : tombstoneStateAfterEvents) {
				replacementRoom = tombstoneReplacement(stateVal);
				if (!replacementRoom.isEmpty())
					break;
			}
		if (replacementRoom.isEmpty()) {
			const QJsonArray timelineEvents = roomData.value(QStringLiteral("timeline"))
				.toObject().value(QStringLiteral("events")).toArray();
			for (const QJsonValue &eventVal : timelineEvents) {
				replacementRoom = tombstoneReplacement(eventVal);
				if (!replacementRoom.isEmpty())
					break;
			}
		}
		if (!replacementRoom.isEmpty() && replacementRoom != roomId) {
			FReplacedRoomIds.insert(roomId);
			FRooms.remove(roomId);
			FLimitedRooms.remove(roomId);
			FRoomPrevBatch.remove(roomId);
			continue;
		}
		ProtocolRoom &room = FRooms[roomId];
		room.id = roomId;
		if (room.roomType.isEmpty())
			room.roomType = FKnownRoomTypes.value(roomId);
		room.membership = QStringLiteral("join");
		room.isJoined = true;
		room.isAvailable = true;
		const QJsonObject summary = roomData.value(QStringLiteral("summary")).toObject();
		if (summary.contains(QStringLiteral("m.joined_member_count")))
			room.memberCount = summary.value(QStringLiteral("m.joined_member_count")).toInt();
		const QJsonObject roomUnread = roomData.value(QStringLiteral("unread_notifications")).toObject();
		room.notificationCount = roomUnread.value(QStringLiteral("notification_count")).toInt();
		room.highlightCount = roomUnread.value(QStringLiteral("highlight_count")).toInt();
		QJsonArray stateEvents = roomData.value("state").toObject().value("events").toArray();
		const QJsonArray stateAfterEvents = roomData.value(QStringLiteral("state_after"))
			.toObject().value(QStringLiteral("events")).toArray();
		for (const QJsonValue &stateAfterValue : stateAfterEvents)
			stateEvents.append(stateAfterValue);
		for (const QJsonValue &stateVal : stateEvents) {
			const QJsonObject stateEvent = stateVal.toObject();
			const QJsonObject content = stateEvent.value("content").toObject();
			const QString stateType = stateEvent.value("type").toString();
			MatrixTimelineEvent storedState;
			storedState.roomId = roomId;
			storedState.eventId = stateEvent.value(QStringLiteral("event_id")).toString();
			storedState.eventType = stateType;
			storedState.sender = stateEvent.value(QStringLiteral("sender")).toString();
			storedState.originTs = stateEvent.value(QStringLiteral("origin_server_ts")).toVariant().toLongLong();
			storedState.content = content.value(QStringLiteral("body")).toString();
			storedState.metadata.insert(QStringLiteral("state_key"),
				stateEvent.value(QStringLiteral("state_key")).toString());
			for (auto contentIt = content.constBegin(); contentIt != content.constEnd(); ++contentIt)
				storedState.metadata.insert(contentIt.key(), contentIt.value().toVariant());
			if (storedState.isValid())
				persistentEvents[roomId].append(storedState);
			if (stateType == QStringLiteral("m.room.name"))
				room.name = content.value("name").toString();
			else if (stateType == QStringLiteral("m.room.topic"))
				room.subject = content.value("topic").toString();
			else if (stateType == QStringLiteral("m.room.avatar"))
				room.avatarUrl = content.value("url").toString();
			else if (stateType == QStringLiteral("m.room.encryption"))
				room.isEncrypted = true;
			else if (stateType == QStringLiteral("m.room.create")) {
				const QString roomType = content.value(QStringLiteral("type")).toString();
				room.roomType = roomType;
				const QString roomVersion = content.value(QStringLiteral("room_version")).toString();
				bool roomTypeSaved = databaseBool([&](MatrixDatabase &database) {
					return database.saveRoomType(roomId, roomType, roomVersion);
				});
				if (!roomTypeSaved)
					qWarning() << "Failed to persist Matrix room type" << roomId;
			}
			else if (stateType == QStringLiteral("m.room.member")) {
				const QString userId = stateEvent.value("state_key").toString();
				const QString membership = content.value("membership").toString();
				ProtocolRosterEntry member;
				member.id = userId;
				member.name = content.value("displayname").toString();
				member.avatarUrl = content.value("avatar_url").toString();
				member.presence = QStringLiteral("unknown");
				member.isValid = membership == QStringLiteral("join");
				for (int i = 0; i < room.members.size(); ++i)
					if (room.members.at(i).id == userId) { room.members.removeAt(i); break; }
				if (member.isValid)
					room.members.append(member);
				const QString memberEventId = stateEvent.value(QStringLiteral("event_id")).toString();
				bool memberSaved = databaseBool([&](MatrixDatabase &database) {
					return database.saveRoomMember(roomId, userId, membership, member.name,
						member.avatarUrl, memberEventId);
				});
				if (!memberSaved)
					qWarning() << "Failed to persist Matrix room member" << roomId << userId;
				const qint64 memberTimestamp = stateEvent.value(QStringLiteral("origin_server_ts"))
					.toVariant().toLongLong();
				bool memberHistorySaved = databaseBool([&](MatrixDatabase &database) {
					return database.saveRoomMemberHistory(roomId, userId, membership, member.name,
						member.avatarUrl, memberTimestamp, memberEventId);
				});
				if (!memberHistorySaved)
					qWarning() << "Failed to persist Matrix member history" << roomId << userId;
			}
		}

		// Parse ephemeral events
		const QJsonObject ephemeral = roomData.value("ephemeral").toObject();
		const QJsonArray ephemeralEvents = ephemeral.value("events").toArray();
		for (const QJsonValue &eventVal : ephemeralEvents) {
			const QJsonObject event = eventVal.toObject();
			if (event.value("type").toString() == QStringLiteral("m.receipt")) {
				const QJsonObject receiptContent = event.value("content").toObject();
				for (auto eventIt = receiptContent.constBegin(); eventIt != receiptContent.constEnd(); ++eventIt) {
					const QString eventId = eventIt.key();
					const QJsonObject receiptTypes = eventIt.value().toObject();
					for (auto typeIt = receiptTypes.constBegin(); typeIt != receiptTypes.constEnd(); ++typeIt) {
						const QString receiptType = typeIt.key();
						const QJsonObject userReceipts = typeIt.value().toObject();
						for (auto userIt = userReceipts.constBegin(); userIt != userReceipts.constEnd(); ++userIt) {
							const QString userId = userIt.key();
							const QJsonObject receipt = userIt.value().toObject();
							const qint64 timestamp = receipt.value(QStringLiteral("ts")).toVariant().toLongLong();
							const bool receiptSaved = databaseBool([&](MatrixDatabase &database) {
								return database.saveReceipt(roomId, userId, eventId, receiptType, timestamp);
							});
							if (!receiptSaved)
								qWarning() << "Failed to persist Matrix receipt" << roomId << userId << eventId;
							MatrixReceipt update{roomId, userId, eventId, receiptType, timestamp};
							emit receiptReceived(update);
						}
					}
				}
				continue;
			}
			if (event.value("type").toString() != QStringLiteral("m.typing"))
				continue;
			ProtocolTypingUpdate update;
			update.accountId = FUserId;
			update.conversationId = roomId;
			const QJsonArray userArray = event.value("content").toObject().value("user_ids").toArray();
			for (const QJsonValue &user : userArray)
				update.userIds.append(user.toString());
			emit typingChanged(update);
		}

		// Parse timeline events
		const QJsonObject timeline = roomData.value("timeline").toObject();
		if (timeline.value(QStringLiteral("limited")).toBool())
			FLimitedRooms.insert(roomId);
		else
			FLimitedRooms.remove(roomId);
		const QString prevBatch = timeline.value(QStringLiteral("prev_batch")).toString();
		if (!prevBatch.isEmpty())
			FRoomPrevBatch.insert(roomId, prevBatch);
		const QString boundaryToken = prevBatch.isEmpty() ? FRoomPrevBatch.value(roomId) : prevBatch;
		const bool boundarySaved = databaseBool([&](MatrixDatabase &database) {
			return database.saveRoomTimelineBoundary(roomId, boundaryToken,
				timeline.value(QStringLiteral("limited")).toBool());
		});
		if (!boundarySaved)
			qWarning() << "Failed to persist Matrix timeline boundary" << roomId;
		const QJsonArray roomAccountEvents = roomData.value(QStringLiteral("account_data"))
			.toObject().value(QStringLiteral("events")).toArray();
		for (const QJsonValue &accountValue : roomAccountEvents) {
			const QJsonObject accountEvent = accountValue.toObject();
			const QString accountType = accountEvent.value(QStringLiteral("type")).toString();
			const QJsonObject content = accountEvent.value(QStringLiteral("content")).toObject();
			const QByteArray accountJson = QJsonDocument(content).toJson(QJsonDocument::Compact);
			const bool accountDataSaved = databaseBool([&](MatrixDatabase &database) {
				return database.saveRoomAccountData(roomId, accountType, accountJson);
			});
			if (!accountDataSaved)
				qWarning() << "Failed to persist Matrix room account data" << roomId << accountType;
			if (accountType == QStringLiteral("m.fully_read"))
				room.fullyReadEventId = content.value(QStringLiteral("event_id")).toString();
			else if (accountType == QStringLiteral("m.read_marker")) {
				room.fullyReadEventId = content.value(QStringLiteral("fully_read")).toString();
				room.readEventId = content.value(QStringLiteral("read")).toString();
			} else if (accountType == QStringLiteral("m.tag")) {
				room.tags.clear();
				const QJsonObject tags = content.value(QStringLiteral("tags")).toObject();
				for (auto tagIt = tags.constBegin(); tagIt != tags.constEnd(); ++tagIt)
					room.tags.insert(tagIt.key());
			} else if (accountType == QStringLiteral("m.marked_unread"))
				room.markedUnread = content.value(QStringLiteral("unread")).toBool();
		}
		const bool roomStateSaved = databaseBool([&](MatrixDatabase &database) {
			return database.saveRoomState(roomId, room.name, room.subject, room.avatarUrl,
				room.membership, FDirectRoomIds.contains(roomId), room.isEncrypted,
				replacementRoom, boundaryToken);
		});
		if (!roomStateSaved)
			qWarning() << "Failed to persist Matrix room state" << roomId;
		if (room.name.isEmpty())
			requestRoomName(roomId);
		requestJoinedMembers(roomId);
		const QJsonArray eventsArray = timeline.value("events").toArray();

		for (const QJsonValue &eventVal : eventsArray) {
			const QJsonObject event = eventVal.toObject();

			QString eventType = event.value("type").toString();
			MatrixNotificationEvent notificationEvent;
			notificationEvent.eventId = event.value(QStringLiteral("event_id")).toString();
			notificationEvent.roomId = roomId;
			notificationEvent.roomType = room.roomType;
			notificationEvent.sender = event.value(QStringLiteral("sender")).toString();
			notificationEvent.type = eventType;
			notificationEvent.content = event.value(QStringLiteral("content")).toObject();
			notificationEvent.timestamp = event.value(QStringLiteral("origin_server_ts")).toVariant().toLongLong();
			notificationEvent.historical = !FInitialSyncComplete;
		if (!notificationEvent.eventId.isEmpty())
			emit notificationEventReceived(notificationEvent);
			const bool wasEncrypted = eventType == QStringLiteral("m.room.encrypted");
			const QString outerEventType = eventType;
			const QJsonObject eventContent = event.value("content").toObject();
			QJsonObject decryptedContent = eventContent;
			bool decrypted = false;
			MatrixMegolmDecryptResult decryptResult;
			if (eventType == QStringLiteral("m.room.encrypted") &&
				eventContent.value(QStringLiteral("algorithm")).toString() ==
				QStringLiteral("m.megolm.v1.aes-sha2")) {
				const QString megolmSessionId = eventContent.value(QStringLiteral("session_id")).toString();
				QByteArray sessionPickle;
				QString storedRoomId;
				QString storedSenderKey;
				runDatabase([&](MatrixDatabase &database) {
				database.loadMegolmSession(megolmSessionId, storedRoomId, storedSenderKey, sessionPickle);
			});
			decryptResult = FOlmCrypto.decryptMegolmDetailedWithPickle(
				megolmSessionId,
				eventContent.value(QStringLiteral("ciphertext")).toString().toUtf8(), sessionPickle);
				if (decryptResult.succeeded()) {
					bool replayDetected = false;
					const QString eventId = event.value(QStringLiteral("event_id")).toString();
					bool messageIndexSaved = false;
					runDatabase([&](MatrixDatabase &database) {
						messageIndexSaved = database.checkAndSaveMegolmMessageIndex(
							eventContent.value(QStringLiteral("session_id")).toString(),
							decryptResult.messageIndex, eventId, replayDetected);
				});
					if (!messageIndexSaved) {
						decryptResult.status = QStringLiteral("failed");
						decryptResult.error = QStringLiteral("message_index_persistence_failed");
					} else if (replayDetected) {
						decryptResult.status = QStringLiteral("replay");
						decryptResult.error = QStringLiteral("message_index_event_conflict");
						decryptResult.plaintext.clear();
					} else {
						bool sessionSaved = false;
						runDatabase([&](MatrixDatabase &database) {
							sessionSaved = FOlmCrypto.persistMegolmSession(database, megolmSessionId,
								storedRoomId, storedSenderKey);
						});
						if (!sessionSaved) {
							decryptResult.status = QStringLiteral("failed");
							decryptResult.error = QStringLiteral("session_persistence_failed");
						}
					}
				}
				const QByteArray plaintext = decryptResult.plaintext;
				QJsonParseError decryptedError;
				const QJsonDocument decryptedDocument = QJsonDocument::fromJson(plaintext, &decryptedError);
				if (decryptedError.error == QJsonParseError::NoError && decryptedDocument.isObject()) {
					const QJsonObject decryptedEvent = decryptedDocument.object();
					if (decryptedEvent.value(QStringLiteral("content")).isObject()) {
						decryptedContent = decryptedEvent.value(QStringLiteral("content")).toObject();
						const QString decryptedEventType = decryptedEvent.value(QStringLiteral("type")).toString();
						if (!decryptedEventType.isEmpty())
							eventType = decryptedEventType;
						decrypted = true;
					}
				}
			}
			if (eventType == QStringLiteral("m.room.encryption")) {
				room.isEncrypted = true;
				continue;
			}
			if (eventType == QStringLiteral("m.room.name")) {
				room.name = eventContent.value("name").toString();
				continue;
			}
			if (eventType == QStringLiteral("m.room.topic")) {
				room.subject = eventContent.value("topic").toString();
				continue;
			}
			if (eventType == QStringLiteral("m.room.avatar")) {
				room.avatarUrl = eventContent.value("url").toString();
				continue;
			}
			if (eventType == QStringLiteral("m.room.member")) {
				const QString userId = event.value("state_key").toString();
				const QString membership = eventContent.value("membership").toString();
				ProtocolRosterEntry member;
				member.id = userId;
				member.name = eventContent.value("displayname").toString();
				member.avatarUrl = eventContent.value("avatar_url").toString();
				member.presence = QStringLiteral("unknown");
				member.isValid = membership == QStringLiteral("join");
				for (int i = 0; i < room.members.size(); ++i)
					if (room.members.at(i).id == userId) { room.members.removeAt(i); break; }
				if (member.isValid)
					room.members.append(member);
				continue;
			}

			if (eventType != QStringLiteral("m.room.message") &&
				eventType != QStringLiteral("m.reaction") &&
				eventType != QStringLiteral("m.sticker") &&
				eventType != QStringLiteral("m.room.encrypted") &&
				eventType != QStringLiteral("m.room.redaction")) {
				continue;
			}

			if (eventType == QStringLiteral("m.room.redaction")) {
				QString redactedEventId = event.value(QStringLiteral("redacts")).toString();
				if (redactedEventId.isEmpty())
					redactedEventId = eventContent.value(QStringLiteral("redacts")).toString();
				if (!redactedEventId.isEmpty()) {
					MatrixTextEvent *redacted = nullptr;
					QList<MatrixTextEvent> &history = FMessageHistory[roomId];
					for (MatrixTextEvent &candidate : history)
						if (candidate.eventId == redactedEventId) {
							redacted = &candidate;
							break;
						}
					for (MatrixTextEvent &candidate : events)
						if (candidate.eventId == redactedEventId) {
							redacted = &candidate;
							break;
						}
					if (redacted) {
						redacted->content = QStringLiteral("message deleted");
						redacted->messageType = QStringLiteral("m.text");
						redacted->attachments.clear();
						redacted->metadata.remove(QStringLiteral("body"));
						redacted->metadata.remove(QStringLiteral("format"));
						redacted->metadata.remove(QStringLiteral("formatted_body"));
						redacted->metadata.remove(QStringLiteral("m.new_content"));
						redacted->metadata.remove(QStringLiteral("m.relates_to"));
						redacted->metadata.remove(QStringLiteral("url"));
						redacted->metadata.remove(QStringLiteral("file"));
						redacted->metadata.remove(QStringLiteral("thumbnail_url"));
						redacted->metadata.remove(QStringLiteral("filename"));
						redacted->metadata.remove(QStringLiteral("attachments"));
						redacted->metadata.remove(QStringLiteral("attachment_url"));
						redacted->metadata.remove(QStringLiteral("attachment_file"));
						redacted->metadata.remove(QStringLiteral("attachment_thumbnail_url"));
						redacted->metadata.remove(QStringLiteral("image_data"));
						redacted->metadata.remove(QStringLiteral("image_url"));
						redacted->metadata.remove(QStringLiteral("file_path"));
						redacted->metadata.insert(QStringLiteral("msgtype"), QStringLiteral("m.text"));
						redacted->metadata.insert(QStringLiteral("redacted"), true);
						MatrixTimelineEvent replacement;
						replacement.roomId = redacted->roomId;
						replacement.eventId = redacted->eventId;
						replacement.eventType = redacted->eventType;
						replacement.sender = redacted->userId;
						replacement.originTs = redacted->timestamp.toLongLong();
						replacement.messageType = redacted->messageType;
						replacement.content = redacted->content;
						for (auto metadataIt = redacted->metadata.constBegin();
							metadataIt != redacted->metadata.constEnd(); ++metadataIt)
							replacement.metadata.insert(metadataIt.key(), metadataIt.value());
						runDatabase([&](MatrixDatabase &database) {
							database.updateTimelineEvent(replacement);
						});
					}
					emit messageHistoryChanged(roomId, history);
				}
			}

			MatrixTextEvent matrixEvent;
			matrixEvent.roomId = roomId;
			matrixEvent.eventId = event.value("event_id").toString();
			matrixEvent.userId = event.value("sender").toString();
			matrixEvent.metadata.insert(QStringLiteral("sender_is_self"),
				matrixEvent.userId == FUserId);
			for (const ProtocolRosterEntry &member : room.members)
				if (member.id == matrixEvent.userId) {
					if (!member.name.isEmpty())
						matrixEvent.metadata.insert(QStringLiteral("sender_name"), member.name);
					if (!member.avatarUrl.isEmpty())
					{
						QString profileDirectory;
						runDatabase([&](MatrixDatabase &database) {
							profileDirectory = database.profileDirectory();
						});
						const QString avatarDirectory = profileDirectory.isEmpty()
							? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/matrix-avatars")
							: profileDirectory + QStringLiteral("/avatars");
						matrixEvent.metadata.insert(QStringLiteral("sender_avatar"), avatarDirectory + QLatin1Char('/') +
							QString::fromLatin1(QCryptographicHash::hash(member.avatarUrl.toUtf8(), QCryptographicHash::Sha256).toHex()) +
							QStringLiteral(".bin"));
					}
					break;
				}
			QString historicalName;
			runDatabase([&](MatrixDatabase &database) {
				historicalName = database.historicalMemberDisplayName(
					roomId, matrixEvent.userId,
					event.value(QStringLiteral("origin_server_ts")).toVariant().toLongLong());
			});
			if (!historicalName.isEmpty())
				matrixEvent.metadata.insert(QStringLiteral("sender_name"), historicalName);
			const QString transactionId = event.value(QStringLiteral("unsigned")).toObject()
				.value(QStringLiteral("transaction_id")).toString();
			matrixEvent.timestamp = QString::number(event.value("origin_server_ts").toVariant().toLongLong());
			matrixEvent.eventType = eventType;
			matrixEvent.metadata.insert(QStringLiteral("room_type"), room.roomType);
			const QJsonObject messageContent = wasEncrypted ? decryptedContent : eventContent;
			matrixEvent.messageType = eventType == QStringLiteral("m.room.encrypted")
				? (decrypted ? decryptedContent.value(QStringLiteral("msgtype")).toString()
				             : QStringLiteral("m.encrypted"))
				: eventType == QStringLiteral("m.room.redaction")
				? QStringLiteral("m.redaction")
				: messageContent.value(QStringLiteral("msgtype")).toString();
			matrixEvent.content = messageContent.value(QStringLiteral("body")).toString();
			if (eventType == QStringLiteral("m.room.message")) {
				matrixEvent.metadata.insert(QStringLiteral("msgtype"),
					messageContent.value(QStringLiteral("msgtype")).toString());
				matrixEvent.metadata.insert(QStringLiteral("sender_id"), matrixEvent.userId);
				if (messageContent.value(QStringLiteral("msgtype")).toString() == QStringLiteral("m.text"))
					matrixEvent.metadata.insert(QStringLiteral("body"), matrixEvent.content);
			}
			const qint64 eventTimestamp = event.value("origin_server_ts").toVariant().toLongLong();
			if (eventTimestamp >= room.lastMessageTs) {
				room.lastEventId = matrixEvent.eventId;
				room.lastMessage = matrixEvent.content;
				room.lastMessageTs = eventTimestamp;
			}
			if (matrixEvent.content.isEmpty())
				matrixEvent.content = messageContent.value("url").toString();
			if (matrixEvent.content.isEmpty())
				matrixEvent.content = messageContent.value("filename").toString();
			if (matrixEvent.content.isEmpty() && eventType == QStringLiteral("m.room.encrypted"))
				matrixEvent.content = QStringLiteral("[encrypted Matrix event]");
			QStringList attachmentUrls;
			for (const QString &attachmentKey : {QStringLiteral("url"), QStringLiteral("file"),
				QStringLiteral("thumbnail_url")}) {
				const QString attachment = messageContent.value(attachmentKey).toString();
				if (!attachment.isEmpty()) {
					attachmentUrls.append(attachment);
					matrixEvent.metadata.insert(QStringLiteral("attachment_") + attachmentKey, attachment);
				}
			}
			for (auto it = eventContent.constBegin(); it != eventContent.constEnd(); ++it)
				matrixEvent.metadata.insert(it.key(), it.value().toVariant());
			for (const QString &formattedField : {QStringLiteral("format"), QStringLiteral("formatted_body")})
				if (messageContent.contains(formattedField))
					matrixEvent.metadata.insert(formattedField,
						messageContent.value(formattedField).toVariant());
			if (!attachmentUrls.isEmpty())
				matrixEvent.metadata.insert(QStringLiteral("attachments"), attachmentUrls);
			matrixEvent.attachments = attachmentUrls;
			matrixEvent.metadata.insert(QStringLiteral("event_type"), eventType);
			if (!FInitialSyncComplete)
				matrixEvent.metadata.insert(QStringLiteral("historical"), true);
			if (wasEncrypted) {
				matrixEvent.metadata.insert(QStringLiteral("outer_event_type"), outerEventType);
				if (decrypted)
					matrixEvent.metadata.insert(QStringLiteral("decrypted_event_type"), eventType);
			}
			matrixEvent.metadata.insert(QStringLiteral("message_type"), matrixEvent.messageType);
			bool highlighted = false;
			for (const QString &pattern : FHighlightBodyPatterns) {
				const QString needle = pattern.trimmed().remove(QLatin1Char('*'));
				if (!needle.isEmpty() && matrixEvent.content.contains(needle, Qt::CaseInsensitive)) {
					highlighted = true;
					break;
				}
			}
			for (const QString &pattern : FHighlightUserPatterns)
				if (matrixEvent.userId.compare(pattern, Qt::CaseInsensitive) == 0)
					highlighted = true;
			if (highlighted)
				matrixEvent.metadata.insert(QStringLiteral("highlight"), true);
			if (!transactionId.isEmpty())
				matrixEvent.metadata.insert(QStringLiteral("replaces_txn_id"), transactionId);
			if (wasEncrypted) {
				matrixEvent.metadata.insert(QStringLiteral("decryption_status"),
					decryptResult.status.isEmpty() ?
						(decrypted ? QStringLiteral("decrypted") : QStringLiteral("pending")) :
						decryptResult.status);
				if (!decryptResult.error.isEmpty())
					matrixEvent.metadata.insert(QStringLiteral("decryption_error"), decryptResult.error);
				if (decryptResult.succeeded())
					matrixEvent.metadata.insert(QStringLiteral("message_index"),
						static_cast<qlonglong>(decryptResult.messageIndex));
				matrixEvent.metadata.insert(QStringLiteral("encryption_algorithm"),
					eventContent.value(QStringLiteral("algorithm")).toString());
			}
			if (eventType == QStringLiteral("m.room.redaction")) {
				const QString contentRedacts = eventContent.value(QStringLiteral("redacts")).toString();
				matrixEvent.metadata.insert(QStringLiteral("redacts"),
					contentRedacts.isEmpty()
						? event.value(QStringLiteral("redacts")).toString()
						: contentRedacts);
			}
			const QJsonObject relation = messageContent.value(QStringLiteral("m.relates_to")).toObject();
			if (!relation.isEmpty()) {
				const QString relationType = relation.value(QStringLiteral("rel_type")).toString();
				const QString relatedEventId = relation.value(QStringLiteral("event_id")).toString();
				const QString inReplyTo = MatrixReply::replyEventId(messageContent);
				matrixEvent.metadata.insert(QStringLiteral("relation_type"), relationType);
				matrixEvent.metadata.insert(QStringLiteral("related_event_id"), relatedEventId);
				matrixEvent.metadata.insert(QStringLiteral("reply_to_event_id"), inReplyTo);
				if (relationType == QStringLiteral("m.thread")) {
					matrixEvent.metadata.insert(QStringLiteral("thread_root_event_id"), relatedEventId);
					matrixEvent.metadata.insert(QStringLiteral("thread_is_falling_back"),
						relation.value(QStringLiteral("is_falling_back")).toBool());
				}
				if (relationType == QStringLiteral("m.replace"))
					matrixEvent.metadata.insert(QStringLiteral("replacement_content"),
					messageContent.value(QStringLiteral("m.new_content")).toObject().toVariantMap());
				if (relationType == QStringLiteral("m.annotation"))
					matrixEvent.metadata.insert(QStringLiteral("reaction_key"),
						relation.value(QStringLiteral("key")).toString());
			}
			else {
				const QString replyEventId = MatrixReply::replyEventId(messageContent);
				if (!replyEventId.isEmpty())
					matrixEvent.metadata.insert(QStringLiteral("reply_to_event_id"), replyEventId);
			}

			if ((!transactionId.isEmpty() && replacePendingEvent(roomId, transactionId, matrixEvent)) ||
				mergeMessageEvent(matrixEvent))
				events.append(matrixEvent);
		}
	}
	bool metadataCommitted = false;
	runDatabase([&](MatrixDatabase &database) {
		metadataCommitted = database.commitSyncMetadataBatch();
	});
	if (!metadataCommitted) {
		runDatabase([](MatrixDatabase &database) { database.rollbackSyncMetadataBatch(); });
		qWarning() << "Failed to commit Matrix sync metadata transaction";
	}
	// Emit events
	for (const MatrixTextEvent &event : events) {
		MatrixTimelineEvent timelineEvent;
		timelineEvent.roomId = event.roomId;
		timelineEvent.eventId = event.eventId;
		timelineEvent.eventType = event.eventType;
		timelineEvent.sender = event.userId;
		timelineEvent.originTs = event.timestamp.toLongLong();
		timelineEvent.messageType = event.messageType;
		timelineEvent.content = event.content;
		timelineEvent.attachments = event.attachments;
		for (auto metadataIt = event.metadata.constBegin(); metadataIt != event.metadata.constEnd(); ++metadataIt)
			timelineEvent.metadata.insert(metadataIt.key(), metadataIt.value());
		persistentEvents[event.roomId].append(timelineEvent);
	}
	QList<MatrixTimelineEvent> syncEvents;
	for (auto it = persistentEvents.constBegin(); it != persistentEvents.constEnd(); ++it)
		syncEvents.append(it.value());
	if (!nextBatch.isEmpty()) {
		QElapsedTimer persistTimer;
		persistTimer.start();
		bool persisted = false;
		if (FDatabaseWorker)
			QMetaObject::invokeMethod(FDatabaseWorker, "persistSyncBatch", Qt::BlockingQueuedConnection,
				Q_RETURN_ARG(bool, persisted), Q_ARG(QList<MatrixTimelineEvent>, syncEvents),
				Q_ARG(QString, nextBatch));
		if (!persisted) {
			qWarning() << "Failed to persist Matrix sync batch; retaining previous cursor";
			emit syncError(QStringLiteral("Failed to persist Matrix sync batch"));
			QTimer::singleShot(1000, this, [this]() { sync(); });
			return;
		}
		FSyncToken = nextBatch;
	}
	FInitialSyncComplete = true;
	if (wasInitialSync) {
		emit initialSyncCompleted();
	}
	if (!wasInitialSync)
		emitRosterSnapshot();

	if (!wasInitialSync && !events.isEmpty()) {
		saveMessageHistory();
		emit syncReceived(events);

		for (const MatrixTextEvent &event : events) {
			if (event.messageType == QStringLiteral("m.image")) {
				if (event.roomId == FActiveRoomId)
					requestImage(event);
				else
					emit messageReceived(event.toBasicMessage());
				continue;
			}
			emit messageReceived(event.toBasicMessage());
		}
	}
	

	// Keep a one-second floor between completed sync requests. The server-side
	// long-poll remains 30 seconds, while quick empty responses cannot spin the loop.
	QTimer::singleShot(1000, this, [this]() {
		if (!FAccesToken.isEmpty() && !FSyncInFlight)
			sync();
	});
}

QList<MatrixTextEvent> MatrixNetwork::messageHistory(const QString &roomId)
{
	if (!FHistoryLoadedRooms.contains(roomId)) {
		QMetaObject::invokeMethod(this, "loadMessageHistory", Qt::QueuedConnection);
		return FMessageHistory.value(roomId);
	}
	return FMessageHistory.value(roomId);
}

void MatrixNetwork::retryPendingEncryptedEvents(const QString &roomId, const QString &sessionId)
{
	QList<MatrixTextEvent> &history = FMessageHistory[roomId];
	if (history.isEmpty()) {
		QList<MatrixTimelineEvent> storedEvents;
		runDatabase([&](MatrixDatabase &database) {
			storedEvents = database.getEvents(roomId);
		});
		for (const MatrixTimelineEvent &stored : storedEvents) {
			MatrixTextEvent event;
			event.eventId = stored.eventId;
			event.roomId = stored.roomId;
			event.userId = stored.sender;
			event.timestamp = QString::number(stored.originTs);
			event.eventType = stored.eventType;
			event.messageType = stored.messageType;
			event.content = stored.content;
			event.attachments = stored.attachments;
			for (auto metadataIt = stored.metadata.constBegin();
				metadataIt != stored.metadata.constEnd(); ++metadataIt)
				event.metadata.insert(metadataIt.key(), metadataIt.value());
			history.append(event);
		}
	}
	QList<MatrixTextEvent> updated;
	for (MatrixTextEvent &event : history) {
		if (event.eventType != QStringLiteral("m.room.encrypted") ||
			 event.metadata.value(QStringLiteral("decryption_status")).toString() ==
				QStringLiteral("decrypted"))
			continue;
		const QString eventSessionId = event.metadata.value(QStringLiteral("session_id")).toString();
		if (eventSessionId.isEmpty() || (!sessionId.isEmpty() && eventSessionId != sessionId))
			continue;
		const QByteArray ciphertext = event.metadata.value(QStringLiteral("ciphertext")).toString().toUtf8();
		QByteArray sessionPickle;
		QString storedRoomId;
		QString storedSenderKey;
		runDatabase([&](MatrixDatabase &database) {
			database.loadMegolmSession(eventSessionId, storedRoomId, storedSenderKey, sessionPickle);
		});
		const MatrixMegolmDecryptResult result = FOlmCrypto.decryptMegolmDetailedWithPickle(
			eventSessionId, ciphertext, sessionPickle);
		if (!result.succeeded()) {
			qWarning() << "[Matrix-E2EE] pending event decrypt failed:" << roomId
				<< "session:" << eventSessionId << "status:" << result.status
				<< "error:" << result.error;
			if (result.error == QStringLiteral("missing_session") ||
				result.error == QStringLiteral("BAD_SIGNATURE")) {
				if (result.error == QStringLiteral("BAD_SIGNATURE")) {
					FOlmCrypto.clearMegolmSession(eventSessionId);
					runDatabase([&](MatrixDatabase &database) {
						database.removeMegolmSession(eventSessionId);
					});
				}
				requestMissingRoomKey(event.userId,
					event.metadata.value(QStringLiteral("sender_key")).toString(),
					roomId, eventSessionId);
			}
			continue;
		}
		bool sessionSaved = false;
		runDatabase([&](MatrixDatabase &database) {
			sessionSaved = FOlmCrypto.persistMegolmSession(database, eventSessionId,
				storedRoomId, storedSenderKey);
		});
		if (!sessionSaved) {
			qWarning() << "[Matrix-E2EE] pending Megolm session persistence failed:" << eventSessionId;
			continue;
		}
		QJsonParseError parseError;
		const QJsonDocument document = QJsonDocument::fromJson(result.plaintext, &parseError);
		if (parseError.error != QJsonParseError::NoError || !document.isObject())
			continue;
		const QJsonObject decrypted = document.object();
		const QJsonObject content = decrypted.value(QStringLiteral("content")).toObject();
		const QString decryptedEventType = decrypted.value(QStringLiteral("type")).toString();
		if (decryptedEventType != QStringLiteral("m.room.message") &&
			decryptedEventType != QStringLiteral("m.reaction"))
			continue;
		event.eventType = decryptedEventType;
		event.metadata.insert(QStringLiteral("event_type"), decryptedEventType);
		if (decryptedEventType == QStringLiteral("m.room.message")) {
			event.messageType = content.value(QStringLiteral("msgtype")).toString();
			event.content = content.value(QStringLiteral("body")).toString();
			event.metadata.insert(QStringLiteral("msgtype"), event.messageType);
			event.metadata.insert(QStringLiteral("body"), event.content);
			const QString replyEventId = MatrixReply::replyEventId(content);
			if (!replyEventId.isEmpty())
				event.metadata.insert(QStringLiteral("reply_to_event_id"), replyEventId);
		} else {
			const QJsonObject relation = content.value(QStringLiteral("m.relates_to")).toObject();
			event.metadata.insert(QStringLiteral("relation_type"), relation.value(QStringLiteral("rel_type")).toString());
			event.metadata.insert(QStringLiteral("related_event_id"), relation.value(QStringLiteral("event_id")).toString());
			event.metadata.insert(QStringLiteral("reaction_key"), relation.value(QStringLiteral("key")).toString());
		}
		event.metadata.insert(QStringLiteral("decryption_status"), QStringLiteral("decrypted"));
		event.metadata.insert(QStringLiteral("message_index"),
			static_cast<qlonglong>(result.messageIndex));
		MatrixTimelineEvent stored;
		stored.roomId = event.roomId;
		stored.eventId = event.eventId;
		stored.eventType = event.eventType;
		stored.sender = event.userId;
		stored.originTs = event.timestamp.toLongLong();
		stored.messageType = event.messageType;
		stored.content = event.content;
		for (auto metadataIt = event.metadata.constBegin(); metadataIt != event.metadata.constEnd(); ++metadataIt)
			stored.metadata.insert(metadataIt.key(), metadataIt.value());
		runDatabase([&](MatrixDatabase &database) {
			database.updateTimelineEvent(stored);
		});
		updated.append(event);
	}
	// Updated events are emitted individually below. Re-emitting the full room
	// history on every retry turns a no-op key retry into a burst of queued UI
	// messages proportional to the room history size.
	for (const MatrixTextEvent &event : updated)
		emit messageReceived(event.toBasicMessage());
}

QList<MatrixTextEvent> MatrixNetwork::historyBackfill(const QString &roomId, int limit)
{
	if (FServerUrl.isEmpty() || FAccesToken.isEmpty() || !FRooms.contains(roomId))
		return QList<MatrixTextEvent>();

	const QString prevBatch = FRoomPrevBatch.value(roomId);
	if (prevBatch.isEmpty()) {
		qWarning() << "No prev_batch available for room" << roomId;
		return QList<MatrixTextEvent>();
	}

	QUrl url(constructUrl(QStringLiteral("/rooms/%1/messages").arg(roomId)));
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("from"), prevBatch);
	query.addQueryItem(QStringLiteral("dir"), QStringLiteral("b"));
	query.addQueryItem(QStringLiteral("limit"), QString::number(std::min(limit, 50)));
	url.setQuery(query);

	QNetworkRequest request(url);
	request.setAttribute(QNetworkRequest::Http2AllowedAttribute, FUseHttp2);
	request.setRawHeader("Authorization", QByteArray("Bearer ").append(FAccesToken.toUtf8()));

	QNetworkReply *reply = FNetworkAccessManager->get(request);
	QEventLoop loop;
	QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
	loop.exec();

	if (reply->error() != QNetworkReply::NoError) {
		qWarning() << "Backfill error:" << reply->errorString();
		return QList<MatrixTextEvent>();
	}
	if (FRoomPrevBatch.value(roomId) != prevBatch) {
		qWarning() << "Discarding stale backfill response for room" << roomId;
		return QList<MatrixTextEvent>();
	}

	QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
	QJsonObject root = doc.object();
	QJsonArray events = root.value(QStringLiteral("chunk")).toArray();

	QList<MatrixTextEvent> result;
	QList<MatrixTimelineEvent> persistedEvents;
	for (const QJsonValue &val : events) {
		const QJsonObject obj = val.toObject();
		MatrixTextEvent evt;
		evt.roomId = roomId;
		evt.eventId = obj.value(QStringLiteral("event_id")).toString();
		evt.userId = obj.value(QStringLiteral("sender")).toString();

		const QJsonObject contentObj = obj.value(QStringLiteral("content")).toObject();
		evt.eventType = obj.value(QStringLiteral("type")).toString();
		// m.room.message handler
		if (evt.eventType == QStringLiteral("m.room.message")) {
			const QString messageType = contentObj.value(QStringLiteral("msgtype")).toString();
			evt.messageType = messageType;
			if (messageType == QStringLiteral("m.text")) {
				evt.content = contentObj.value(QStringLiteral("body")).toString();
			} else if (messageType == QStringLiteral("m.emote")) {
				QString body = contentObj.value(QStringLiteral("body")).toString();
				if (!body.isEmpty())
					evt.content = QStringLiteral("/me ").append(body);
			} else if (messageType == QStringLiteral("m.notice")) {
				evt.content = contentObj.value(QStringLiteral("body")).toString();
			}
		} else {
			// Non-message event: try body/fallback
			evt.content = contentObj.value(QStringLiteral("body")).toString();
		}

		const qint64 timestamp = obj.value(QStringLiteral("origin_server_ts")).toVariant().toLongLong();
		evt.timestamp = QString::number(timestamp);
		MatrixTimelineEvent stored;
		stored.roomId = roomId;
		stored.eventId = evt.eventId;
		stored.eventType = evt.eventType;
		stored.sender = evt.userId;
		stored.originTs = timestamp;
		stored.messageType = evt.messageType;
		stored.content = evt.content;

		// Deduplication via existing method
		if (mergeMessageEvent(evt, true)) {
			persistedEvents.append(stored);
			result.append(evt);
		}
	}
	if (!persistedEvents.isEmpty()) {
		bool backfillSaved = false;
		runDatabase([&](MatrixDatabase &database) {
			backfillSaved = database.appendTimelineEvents(roomId, persistedEvents);
		});
		if (!backfillSaved)
			qWarning() << "Failed to persist Matrix backfill events" << roomId;
	}

	// Update token if available
	QString nextToken = root.value(QStringLiteral("end")).toString();
	if (!nextToken.isEmpty()) {
		FRoomPrevBatch[roomId] = nextToken;
		bool boundarySaved = false;
		runDatabase([&](MatrixDatabase &database) {
			boundarySaved = database.saveRoomTimelineBoundary(roomId, nextToken,
				FLimitedRooms.contains(roomId));
		});
		if (!boundarySaved)
			qWarning() << "Failed to persist Matrix backfill boundary" << roomId;
	}

	if (!result.isEmpty())
		saveMessageHistory();
	emit messageHistoryChanged(roomId, FMessageHistory.value(roomId));
	return result;
}

bool MatrixNetwork::mergeMessageEvent(const MatrixTextEvent &event, bool fromHistoryBackfill)
{
	if (event.eventId.isEmpty())
		return false;
	QList<MatrixTextEvent> &history = FMessageHistory[event.roomId];
	for (const MatrixTextEvent &existing : history)
		if (existing.eventId == event.eventId)
			return false;
	history.append(event);
	std::sort(history.begin(), history.end(), [](const MatrixTextEvent &left, const MatrixTextEvent &right) {
		const qint64 leftTs = left.timestamp.toLongLong();
		const qint64 rightTs = right.timestamp.toLongLong();
		return leftTs == rightTs ? left.eventId < right.eventId : leftTs < rightTs;
	});

	// A history snapshot replays every event to the UI; new events should be incremental.
	if (!fromHistoryBackfill)
		emit messageReceived(event.toBasicMessage());
	return true;
}

bool MatrixNetwork::replacePendingEvent(const QString &roomId, const QString &transactionId,
	const MatrixTextEvent &serverEvent)
{
	QList<MatrixTextEvent> &history = FMessageHistory[roomId];
	for (MatrixTextEvent &existing : history) {
		if (existing.eventId != transactionId)
			continue;
		existing = serverEvent;
		std::sort(history.begin(), history.end(), [](const MatrixTextEvent &left, const MatrixTextEvent &right) {
			const qint64 leftTs = left.timestamp.toLongLong();
			const qint64 rightTs = right.timestamp.toLongLong();
			return leftTs == rightTs ? left.eventId < right.eventId : leftTs < rightTs;
		});
		runDatabase([&](MatrixDatabase &database) {
			database.replaceTimelineEventId(roomId, transactionId, serverEvent.eventId);
		});
		emit messageHistoryChanged(roomId, history);
		return true;
	}
	return false;
}

void MatrixNetwork::restorePersistedRooms()
{
	QList<MatrixCachedRoom> cachedRooms;
	if (FDatabaseWorker)
		QMetaObject::invokeMethod(FDatabaseWorker, "loadPersistedRooms", Qt::BlockingQueuedConnection,
			Q_RETURN_ARG(QList<MatrixCachedRoom>, cachedRooms));
	int restored = 0;
	for (const MatrixCachedRoom &cached : cachedRooms) {
		const MatrixStoredRoom &stored = cached.room;
		if (stored.roomId.isEmpty() || stored.membership == QStringLiteral("leave"))
			continue;
		ProtocolRoom &room = FRooms[stored.roomId];
		room.id = stored.roomId;
		room.name = stored.name.isEmpty() ? stored.roomId : stored.name;
		room.subject = stored.topic;
		room.avatarUrl = stored.avatarUrl;
		room.membership = stored.membership;
		room.isJoined = stored.membership == QStringLiteral("join");
		room.isDirect = stored.isDirect;
		room.isEncrypted = stored.isEncrypted;
		room.isAvailable = true;
		if (!cached.previousBatch.isEmpty()) {
			FRoomPrevBatch.insert(stored.roomId, cached.previousBatch);
			if (cached.limited)
				FLimitedRooms.insert(stored.roomId);
		}
		for (const MatrixStoredMember &storedMember : cached.members) {
			ProtocolRosterEntry member;
			member.id = storedMember.userId;
			member.name = storedMember.displayName;
			member.avatarUrl = storedMember.avatarUrl;
			member.presence = QStringLiteral("unknown");
			member.isValid = true;
			room.members.append(member);
		}
		++restored;
	}
	if (restored > 0) {

		QList<ProtocolRoom> rooms;
		for (const ProtocolRoom &room : std::as_const(FRooms))
			rooms.append(room);
		emit rosterChanged(rooms);
	}
}

void MatrixNetwork::loadMessageHistory()
{
	QElapsedTimer loadTimer;
	loadTimer.start();
	FMessageHistory.clear();
	FHistoryLoadedRooms.clear();
	if (FUserId.isEmpty())
		return;
	qsizetype eventCount = 0;
	QStringList storedRoomIds;
	runDatabase([&](MatrixDatabase &database) {
		storedRoomIds = database.roomIds();
	});
	for (const QString &roomId : storedRoomIds) {
		QList<MatrixTextEvent> &history = FMessageHistory[roomId];
		QList<MatrixTimelineEvent> storedEvents;
	runDatabase([&](MatrixDatabase &database) {
		storedEvents = database.getEvents(roomId);
	});
	for (const MatrixTimelineEvent &stored : storedEvents) {
			MatrixTextEvent event;
			event.roomId = stored.roomId;
			event.eventId = stored.eventId;
			event.userId = stored.sender;
			event.content = stored.content;
			event.timestamp = QString::number(stored.originTs);
			event.eventType = stored.eventType;
			event.messageType = stored.messageType;
			for (auto it = stored.metadata.constBegin(); it != stored.metadata.constEnd(); ++it)
				event.metadata.insert(it.key(), it.value());
			event.metadata.insert(QStringLiteral("historical"), true);
			history.append(event);
			++eventCount;
		}
		std::sort(history.begin(), history.end(), [](const MatrixTextEvent &left, const MatrixTextEvent &right) {
			const qint64 leftTs = left.timestamp.toLongLong();
			const qint64 rightTs = right.timestamp.toLongLong();
			return leftTs == rightTs ? left.eventId < right.eventId : leftTs < rightTs;
		});
		emit messageHistoryChanged(roomId, history);
	}

}

void MatrixNetwork::saveMessageHistory() const
{
	// SQLite timeline_events is the single source of truth.
}

void MatrixNetwork::onSendFinished(QNetworkReply *reply)
{
	FSendInFlight = false;
	
	if (reply->error() != QNetworkReply::NoError) {
		QString error = reply->errorString();
		const QString conversationId = reply->property("conversationId").toString();
		const QString transactionId = reply->property("transactionId").toString();
		const QString body = reply->property("body").toString();
		if (reply->property("messageEvent").toBool()) {
			runDatabase([&](MatrixDatabase &database) {
				database.saveOutboxMessage(conversationId, transactionId, body, QStringLiteral("failed"));
			});
			emit messageDeliveryChanged(conversationId, transactionId, QStringLiteral("failed"), QString());
		}
		emit sendError(error);
		return;
	}
	const QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
	const QString serverEventId = response.value(QStringLiteral("event_id")).toString();
	const QString transactionId = reply->property("transactionId").toString();
	const bool messageEvent = reply->property("messageEvent").toBool();
	if (messageEvent)
		runDatabase([&](MatrixDatabase &database) {
			database.removeOutboxMessage(transactionId);
		});
	
	QString messageId = reply->url().toString().split('/').last();
	QString path = reply->url().path();
	QString messageParts = path.split('/').last();
	
	// For PUT /rooms/*/send/..., last part is the txn_id
	// For other patterns, use the URL as-is
	if (!messageParts.startsWith(QStringLiteral("m_"))) {
		messageId = messageParts;  // Use the message ID or txn_id
	}
	
	if (messageEvent) {
		emit messageSent(serverEventId.isEmpty() ? messageId : serverEventId);
		emit messageDeliveryChanged(reply->property("conversationId").toString(),
			reply->property("transactionId").toString(), QStringLiteral("sent"), serverEventId);
	}
}

void MatrixNetwork::sendForwardedRoomKey(const QString &userId, const QString &deviceId,
                                         const QString &roomId, const QString &sessionId,
                                         const QString &requestId, quint32 minimumIndex)
{
	QString storedRoomId;
	QString megolmSenderKey;
	QByteArray storedPickle;
	QString storedClaimedEd25519;
	QString storedForwardingChainJson;
	bool sessionLoaded = false;
	runDatabase([&](MatrixDatabase &database) {
		sessionLoaded = database.loadMegolmSession(sessionId, storedRoomId,
			megolmSenderKey, storedPickle, &storedClaimedEd25519,
			&storedForwardingChainJson);
	});
	bool recipientKeyChanged = false;
	runDatabase([&](MatrixDatabase &database) {
		recipientKeyChanged = database.deviceKeyChanged(userId, deviceId);
	});
	if (!sessionLoaded || storedRoomId != roomId || megolmSenderKey.isEmpty() || recipientKeyChanged)
		return;
	const QByteArray sessionKey = FOlmCrypto.exportMegolmSessionWithPickle(sessionId, storedPickle,
		minimumIndex);
	if (sessionKey.isEmpty())
		return;
	const QString claimedEd25519 = storedClaimedEd25519;
	if (claimedEd25519.isEmpty()) {
		qWarning() << "[Matrix-E2EE] cannot forward room key without claimed Ed25519 key:" << sessionId;
		return;
	}
	QJsonObject forwardedContent;
	forwardedContent.insert(QStringLiteral("algorithm"), QStringLiteral("m.megolm.v1.aes-sha2"));
	forwardedContent.insert(QStringLiteral("room_id"), roomId);
	forwardedContent.insert(QStringLiteral("session_id"), sessionId);
	forwardedContent.insert(QStringLiteral("session_key"), QString::fromUtf8(sessionKey));
	forwardedContent.insert(QStringLiteral("sender_key"), megolmSenderKey);
	forwardedContent.insert(QStringLiteral("sender_claimed_ed25519_key"), claimedEd25519);
	const QJsonDocument storedForwardingChain = QJsonDocument::fromJson(
		storedForwardingChainJson.toUtf8());
	forwardedContent.insert(QStringLiteral("forwarding_curve25519_key_chain"),
		storedForwardingChain.isArray() ? storedForwardingChain.array() : QJsonArray());
	QJsonParseError identityError;
	const QJsonObject identity = QJsonDocument::fromJson(
		FOlmCrypto.identityKeysJson(), &identityError).object();
	const QString senderKey = identity.value(QStringLiteral("curve25519")).toString();
	const QString senderEd25519 = identity.value(QStringLiteral("ed25519")).toString();
	if (senderKey.isEmpty() || senderEd25519.isEmpty())
		return;
	QByteArray deviceKeysJson;
	bool deviceKeysLoaded = false;
	runDatabase([&](MatrixDatabase &database) {
		deviceKeysLoaded = database.loadDeviceKeys(userId, deviceId, deviceKeysJson);
	});
	if (!deviceKeysLoaded)
		return;
	const QJsonObject deviceKeys = QJsonDocument::fromJson(deviceKeysJson).object()
		.value(QStringLiteral("keys")).toObject();
	const QString recipientKey = deviceKeys.value(QStringLiteral("curve25519:%1")
		.arg(deviceId)).toString();
	const QString recipientEd25519 = deviceKeys.value(QStringLiteral("ed25519:%1")
		.arg(deviceId)).toString();
	if (recipientKey.isEmpty() || recipientEd25519.isEmpty())
		return;
	const QJsonObject olmPayload{
		{QStringLiteral("sender"), FUserId},
		{QStringLiteral("sender_device"), FDeviceId},
		{QStringLiteral("recipient"), userId},
		{QStringLiteral("keys"), QJsonObject{{QStringLiteral("ed25519"), senderEd25519}}},
		{QStringLiteral("recipient_keys"), QJsonObject{{QStringLiteral("ed25519"), recipientEd25519}}},
		{QStringLiteral("type"), QStringLiteral("m.forwarded_room_key")},
		{QStringLiteral("content"), forwardedContent}};
	int messageType = -1;
	QString olmSessionId;
	QByteArray olmSessionPickle;
	bool olmSessionLoaded = false;
	runDatabase([&](MatrixDatabase &database) {
		olmSessionLoaded = database.loadOlmSession(userId, deviceId, olmSessionId,
			olmSessionPickle);
	});
	if (!olmSessionLoaded || FOlmCrypto.olmSessionIdWithPickle(userId, deviceId,
		olmSessionId, olmSessionPickle).isEmpty())
		return;
	QByteArray encryptedOlmPickle;
	QByteArray ciphertext = FOlmCrypto.encryptOlmData(userId, deviceId,
		QJsonDocument(olmPayload).toJson(QJsonDocument::Compact), messageType,
		olmSessionId, encryptedOlmPickle);
	if (!ciphertext.isEmpty()) {
		bool olmSessionSaved = false;
		runDatabase([&](MatrixDatabase &database) {
			olmSessionSaved = database.saveOlmSession(userId, deviceId, olmSessionId,
				encryptedOlmPickle);
		});
		if (!olmSessionSaved)
			ciphertext.clear();
	}
	if (ciphertext.isEmpty() || messageType < 0)
		return;

	QJsonObject ciphertextEntry;
	ciphertextEntry.insert(QStringLiteral("body"), QString::fromUtf8(ciphertext));
	ciphertextEntry.insert(QStringLiteral("type"), messageType);
	QJsonObject ciphertextMap;
	ciphertextMap.insert(recipientKey, ciphertextEntry);
	const QJsonObject encryptedContent{
		{QStringLiteral("algorithm"), QStringLiteral("m.olm.v1.curve25519-aes-sha2")},
		{QStringLiteral("sender_key"), senderKey},
		{QStringLiteral("ciphertext"), ciphertextMap}};
	const QString txnId = generateTransactionId();
	QJsonObject devices;
	devices.insert(deviceId, encryptedContent);
	QJsonObject users;
	users.insert(userId, devices);
	QJsonObject payload;
	payload.insert(QStringLiteral("messages"), users);
	QNetworkRequest request(QUrl(constructUrl(QStringLiteral("/_matrix/client/v3/sendToDevice/m.room.encrypted/%1")
		.arg(QUrl::toPercentEncoding(txnId)))));
	request.setRawHeader("Authorization", QByteArray("Bearer ") + FAccesToken.toUtf8());
	request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
	QNetworkReply *reply = FNetworkAccessManager->put(request,
		QJsonDocument(payload).toJson(QJsonDocument::Compact));
	bool shareSaved = false;
	runDatabase([&](MatrixDatabase &database) {
		shareSaved = database.saveMegolmKeyShare(sessionId, userId, deviceId);
	});
	if (!shareSaved)
		qWarning() << "[Matrix-E2EE] failed to persist forwarded Megolm share:" << sessionId;
	reply->setProperty("requestType", QStringLiteral("to_device"));
	reply->setProperty("requestId", requestId);
}

bool MatrixNetwork::sendEncryptedToDeviceEvent(const QString &userId, const QString &deviceId,
                                                const QString &eventType, const QJsonObject &eventContent,
                                                bool forceNewSession)
{
	if (userId.isEmpty() || deviceId.isEmpty() || eventType.isEmpty() ||
		(eventContent.isEmpty() && eventType != QStringLiteral("m.dummy")))
		return false;
	if (forceNewSession) {
		const QString recoveryKey = userId + QLatin1Char('\n') + deviceId;
		if (!FPendingForcedOlmEvents.contains(recoveryKey)) {
			QString oldSessionId;
			QByteArray oldSessionPickle;
			runDatabase([&](MatrixDatabase &database) {
				database.loadOlmSession(userId, deviceId, oldSessionId, oldSessionPickle);
			});
			FOlmCrypto.clearOlmSession(userId, deviceId);
			qWarning() << "[Matrix-E2EE] replaced active outbound Olm session; preserved persisted decrypt candidates:"
				<< userId << deviceId << oldSessionId;
			FPendingForcedOlmEvents.insert(recoveryKey, qMakePair(eventType, eventContent));
			claimOneTimeKey(userId, deviceId);
		}
		return true;
	}
	QJsonParseError identityError;
	const QJsonObject identity = QJsonDocument::fromJson(FOlmCrypto.identityKeysJson(),
		&identityError).object();
	const QString senderCurve25519 = identity.value(QStringLiteral("curve25519")).toString();
	const QString senderEd25519 = identity.value(QStringLiteral("ed25519")).toString();
	if (identityError.error != QJsonParseError::NoError || senderCurve25519.isEmpty() ||
		senderEd25519.isEmpty())
		return false;
	QByteArray deviceKeysJson;
	QString olmSessionId;
	QByteArray olmSessionPickle;
	bool deviceKeysLoaded = false;
	bool olmSessionLoaded = false;
	runDatabase([&](MatrixDatabase &database) {
		deviceKeysLoaded = database.loadDeviceKeys(userId, deviceId, deviceKeysJson);
		olmSessionLoaded = database.loadOlmSession(userId, deviceId, olmSessionId, olmSessionPickle);
	});
	if (!deviceKeysLoaded || !olmSessionLoaded ||
		FOlmCrypto.olmSessionIdWithPickle(userId, deviceId, olmSessionId, olmSessionPickle).isEmpty())
		return false;
	const QJsonObject deviceKeys = QJsonDocument::fromJson(deviceKeysJson).object()
		.value(QStringLiteral("keys")).toObject();
	const QString recipientCurve25519 = deviceKeys.value(
		QStringLiteral("curve25519:%1").arg(deviceId)).toString();
	const QString recipientEd25519 = deviceKeys.value(
		QStringLiteral("ed25519:%1").arg(deviceId)).toString();
	if (recipientCurve25519.isEmpty() || recipientEd25519.isEmpty())
		return false;
	const QJsonObject olmPayload{
		{QStringLiteral("sender"), FUserId},
		{QStringLiteral("sender_device"), FDeviceId},
		{QStringLiteral("recipient"), userId},
		{QStringLiteral("keys"), QJsonObject{{QStringLiteral("ed25519"), senderEd25519}}},
		{QStringLiteral("recipient_keys"), QJsonObject{{QStringLiteral("ed25519"), recipientEd25519}}},
		{QStringLiteral("type"), eventType},
		{QStringLiteral("content"), eventContent}};
	int messageType = -1;
	QByteArray updatedPickle;
	const QByteArray ciphertext = FOlmCrypto.encryptOlmData(userId, deviceId,
		QJsonDocument(olmPayload).toJson(QJsonDocument::Compact), messageType,
		olmSessionId, updatedPickle);
	if (ciphertext.isEmpty() || messageType < 0)
		return false;
	qWarning() << "[Matrix-E2EE] outbound Olm correlation:"
		<< "local_device:" << FDeviceId
		<< "local_curve_hash:" << QCryptographicHash::hash(senderCurve25519.toUtf8(), QCryptographicHash::Sha256).toHex()
		<< "recipient_user:" << userId
		<< "recipient_device:" << deviceId
		<< "recipient_curve_hash:" << QCryptographicHash::hash(recipientCurve25519.toUtf8(), QCryptographicHash::Sha256).toHex()
		<< "session_id:" << olmSessionId
		<< "message_type:" << messageType;
	bool saved = false;
	runDatabase([&](MatrixDatabase &database) {
		saved = database.saveOlmSession(userId, deviceId, olmSessionId, updatedPickle);
	});
	if (!saved)
		return false;
	const QJsonObject encryptedContent{
		{QStringLiteral("algorithm"), QStringLiteral("m.olm.v1.curve25519-aes-sha2")},
		{QStringLiteral("sender_key"), senderCurve25519},
		{QStringLiteral("ciphertext"), QJsonObject{{recipientCurve25519, QJsonObject{
			{QStringLiteral("body"), QString::fromUtf8(ciphertext)},
			{QStringLiteral("type"), messageType}}}}}};
	const QString txnId = generateTransactionId();
	const QJsonObject messages{{userId, QJsonObject{{deviceId, encryptedContent}}}};
	QNetworkRequest request(QUrl(constructUrl(QStringLiteral("/_matrix/client/v3/sendToDevice/m.room.encrypted/%1")
		.arg(QUrl::toPercentEncoding(txnId)))));
	request.setAttribute(QNetworkRequest::Http2AllowedAttribute, FUseHttp2);
	request.setRawHeader("Authorization", QByteArray("Bearer ") + FAccesToken.toUtf8());
	request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
	QNetworkReply *reply = FNetworkAccessManager->put(request,
		QJsonDocument(QJsonObject{{QStringLiteral("messages"), messages}}).toJson(QJsonDocument::Compact));
	reply->setProperty("requestType", QStringLiteral("to_device"));
	reply->setProperty("e2eeEventType", eventType);
	if (eventType == QStringLiteral("m.dummy")) {
		const QString recoveryKey = userId + QLatin1Char('\n') + deviceId;
		reply->setProperty("e2eeOlmRecovery", FPendingOlmRecoveryDevices.contains(recoveryKey));
		reply->setProperty("e2eeRecoveryUserId", userId);
		reply->setProperty("e2eeRecoveryDeviceId", deviceId);
	}
	return true;
}

void MatrixNetwork::requestMissingRoomKey(const QString &sender, const QString &senderKey,
                                           const QString &roomId, const QString &sessionId,
                                           const QString &targetDeviceId)
{
	if (FAccesToken.isEmpty() || sender.isEmpty() || senderKey.isEmpty() || roomId.isEmpty() || sessionId.isEmpty())
		return;
	if (sessionId == QString(43, QLatin1Char('A'))) {
		qWarning() << "[Matrix-E2EE] ignoring room-key request for placeholder Megolm session"
			<< "room:" << roomId;
		return;
	}
	const QString requestKey = roomId + QLatin1Char('\n') + senderKey + QLatin1Char('\n') + sessionId;
	const qint64 now = QDateTime::currentSecsSinceEpoch();
	if ((FRequestedRoomKeys.contains(requestKey) &&
		FRequestedRoomKeyTimes.value(requestKey) + 120 > now) ||
		FPendingRoomKeyRequests.contains(requestKey))
		return;
	QString requestId = FRoomKeyRequestIds.value(requestKey);
	if (requestId.isEmpty()) {
		requestId = QStringLiteral("key_request.") + generateTransactionId();
		FRoomKeyRequestIds.insert(requestKey, requestId);
		FRoomKeyRequestUsers.insert(requestKey, sender);
	}
	const QJsonObject requestContent{
		{QStringLiteral("action"), QStringLiteral("request")},
		{QStringLiteral("body"), QJsonObject{
			{QStringLiteral("algorithm"), QStringLiteral("m.megolm.v1.aes-sha2")},
			{QStringLiteral("room_id"), roomId},
			{QStringLiteral("sender_key"), senderKey},
			{QStringLiteral("session_id"), sessionId}}},
		{QStringLiteral("request_id"), requestId},
		{QStringLiteral("requesting_device_id"), FDeviceId}};
	QJsonObject messages;
	const QString requestedDevice = targetDeviceId.isEmpty()
		? QStringLiteral("*") : targetDeviceId;
	messages.insert(sender, QJsonObject{{requestedDevice, requestContent}});
	messages.insert(FUserId, QJsonObject{{requestedDevice, requestContent}});
	const QString txnId = generateTransactionId();
	QNetworkRequest request(QUrl(constructUrl(QStringLiteral("/_matrix/client/v3/sendToDevice/m.room_key_request/%1")
		.arg(QUrl::toPercentEncoding(txnId)))));
	request.setAttribute(QNetworkRequest::Http2AllowedAttribute, FUseHttp2);
	request.setRawHeader("Authorization", QByteArray("Bearer ") + FAccesToken.toUtf8());
	request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
	QNetworkReply *reply = FNetworkAccessManager->put(request,
		QJsonDocument(QJsonObject{{QStringLiteral("messages"), messages}})
			.toJson(QJsonDocument::Compact));
	reply->setProperty("requestType", QStringLiteral("to_device"));
	reply->setProperty("e2eeEventType", QStringLiteral("m.room_key_request"));
	FRequestedRoomKeys.insert(requestKey);
	FRequestedRoomKeyTimes.insert(requestKey, now);
	FPendingRoomKeyRequests.remove(requestKey);
	qWarning() << "[Matrix-E2EE] sent plaintext room-key request:" << roomId
		<< "session:" << sessionId << "users:" << sender << FUserId
		<< "target_device:" << requestedDevice;
}

void MatrixNetwork::cancelMissingRoomKeyRequest(const QString &roomId, const QString &senderKey,
                                                 const QString &sessionId)
{
	if (FAccesToken.isEmpty() || roomId.isEmpty() || senderKey.isEmpty() || sessionId.isEmpty())
		return;
	const QString requestKey = roomId + QLatin1Char('\n') + senderKey + QLatin1Char('\n') + sessionId;
	const QString requestId = FRoomKeyRequestIds.value(requestKey);
	if (requestId.isEmpty())
		return;
	const QJsonObject cancellation{
		{QStringLiteral("action"), QStringLiteral("request_cancellation")},
		{QStringLiteral("request_id"), requestId},
		{QStringLiteral("requesting_device_id"), FDeviceId}};
	const QString sender = FRoomKeyRequestUsers.value(requestKey);
	QJsonObject messages;
	if (!sender.isEmpty())
		messages.insert(sender, QJsonObject{{QStringLiteral("*"), cancellation}});
	messages.insert(FUserId, QJsonObject{{QStringLiteral("*"), cancellation}});
	const QString txnId = generateTransactionId();
	QNetworkRequest request(QUrl(constructUrl(QStringLiteral("/_matrix/client/v3/sendToDevice/m.room_key_request/%1")
		.arg(QUrl::toPercentEncoding(txnId)))));
	request.setAttribute(QNetworkRequest::Http2AllowedAttribute, FUseHttp2);
	request.setRawHeader("Authorization", QByteArray("Bearer ") + FAccesToken.toUtf8());
	request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
	QNetworkReply *reply = FNetworkAccessManager->put(request,
		QJsonDocument(QJsonObject{{QStringLiteral("messages"), messages}})
			.toJson(QJsonDocument::Compact));
	reply->setProperty("requestType", QStringLiteral("to_device"));
	reply->setProperty("e2eeEventType", QStringLiteral("m.room_key_request"));
	FRequestedRoomKeys.remove(requestKey);
	FRequestedRoomKeyTimes.remove(requestKey);
	FRoomKeyRequestIds.remove(requestKey);
	FRoomKeyRequestUsers.remove(requestKey);
	qWarning() << "[Matrix-E2EE] cancelled room-key request:" << roomId
		<< "session:" << sessionId;
}

bool MatrixNetwork::distributeOutboundRoomKey(const QString &roomId, const QString &sessionId,
	const QByteArray &sessionKey)
{
	if (roomId.isEmpty() || sessionId.isEmpty() || sessionKey.isEmpty())
		return false;
	QJsonParseError identityError;
	const QJsonObject identity = QJsonDocument::fromJson(FOlmCrypto.identityKeysJson(),
		&identityError).object();
	const QString senderKey = identity.value(QStringLiteral("curve25519")).toString();
	QString senderEd25519 = identity.value(QStringLiteral("ed25519")).toString();
	if (senderKey.isEmpty() || senderEd25519.isEmpty())
		return false;
	QByteArray ownDeviceKeysJson;
	runDatabase([&](MatrixDatabase &database) {
		database.loadDeviceKeys(FUserId, FDeviceId, ownDeviceKeysJson);
	});
	const QJsonObject ownDeviceKeys = QJsonDocument::fromJson(ownDeviceKeysJson).object()
		.value(QStringLiteral("keys")).toObject();
	const QString publishedSenderEd25519 = ownDeviceKeys.value(
		QStringLiteral("ed25519:%1").arg(FDeviceId)).toString();
	if (publishedSenderEd25519.isEmpty()) {
		qWarning() << "[Matrix-E2EE] cannot distribute room key: published own Ed25519 device key missing"
			<< "device:" << FDeviceId;
		return false;
	}
	if (publishedSenderEd25519 != senderEd25519) {
		qWarning() << "[Matrix-E2EE] own Ed25519 identity differs from published device key"
			<< "device:" << FDeviceId
			<< "account_key_hash:" << QCryptographicHash::hash(senderEd25519.toUtf8(), QCryptographicHash::Sha256).toHex()
			<< "published_key_hash:" << QCryptographicHash::hash(publishedSenderEd25519.toUtf8(), QCryptographicHash::Sha256).toHex();
		return false;
	}
	const quint32 shareIndex = static_cast<quint32>(qMax(0,
		FOlmCrypto.outboundMegolmMessageIndexForLoadedSession(roomId)));
	QJsonObject users;
	const auto roomIt = FRooms.constFind(roomId);
	if (roomIt == FRooms.constEnd() || roomIt->members.isEmpty())
		return false;
	for (const ProtocolRosterEntry &member : roomIt->members) {
		if (!member.isValid)
			continue;
		QMap<QString, QByteArray> devices;
		runDatabase([&](MatrixDatabase &database) {
			devices = database.loadDeviceKeysForUser(member.id);
		});
		if (devices.isEmpty()) {
			if (member.id == FUserId)
				continue;
			return false;
		}
		QJsonObject deviceMessages;
		for (auto deviceIt = devices.constBegin(); deviceIt != devices.constEnd(); ++deviceIt) {
			if (member.id == FUserId && deviceIt.key() == FDeviceId)
				continue;
			const QJsonObject deviceKeys = QJsonDocument::fromJson(deviceIt.value()).object()
				.value(QStringLiteral("keys")).toObject();
			const QString recipientKey = deviceKeys.value(QStringLiteral("curve25519:%1")
				.arg(deviceIt.key())).toString();
			const QString recipientEd25519 = deviceKeys.value(QStringLiteral("ed25519:%1")
				.arg(deviceIt.key())).toString();
			bool keyChanged = false;
			runDatabase([&](MatrixDatabase &database) {
				keyChanged = database.deviceKeyChanged(member.id, deviceIt.key());
			});
			QString storedSessionId;
			QByteArray storedSessionPickle;
			bool sessionLoaded = false;
			runDatabase([&](MatrixDatabase &database) {
				sessionLoaded = database.loadOlmSession(member.id, deviceIt.key(),
					storedSessionId, storedSessionPickle);
			});
			if (recipientKey.isEmpty() || recipientEd25519.isEmpty() || keyChanged ||
				!sessionLoaded || FOlmCrypto.olmSessionIdWithPickle(member.id,
					deviceIt.key(), storedSessionId, storedSessionPickle).isEmpty())
				return false;
			const QJsonObject roomKey{{QStringLiteral("algorithm"), QStringLiteral("m.megolm.v1.aes-sha2")},
				{QStringLiteral("room_id"), roomId}, {QStringLiteral("session_id"), sessionId},
				{QStringLiteral("session_key"), QString::fromUtf8(sessionKey)},
				{QStringLiteral("sender_key"), senderKey},
				{QStringLiteral("sender_claimed_ed25519_key"), senderEd25519}};
			const QJsonObject payload{
				{QStringLiteral("sender"), FUserId},
				{QStringLiteral("recipient"), member.id},
				{QStringLiteral("keys"), QJsonObject{{QStringLiteral("ed25519"), senderEd25519}}},
				{QStringLiteral("recipient_keys"), QJsonObject{{QStringLiteral("ed25519"), recipientEd25519}}},
				{QStringLiteral("type"), QStringLiteral("m.room_key")},
				{QStringLiteral("content"), roomKey}};
			int messageType = -1;
			QByteArray encryptedOlmPickle;
			QByteArray ciphertext = FOlmCrypto.encryptOlmData(member.id, deviceIt.key(),
				QJsonDocument(payload).toJson(QJsonDocument::Compact), messageType,
				storedSessionId, encryptedOlmPickle);
			if (!ciphertext.isEmpty()) {
				bool olmSessionSaved = false;
				runDatabase([&](MatrixDatabase &database) {
					olmSessionSaved = database.saveOlmSession(member.id, deviceIt.key(),
						storedSessionId, encryptedOlmPickle);
				});
				if (!olmSessionSaved)
					ciphertext.clear();
			}
			if (ciphertext.isEmpty() || messageType < 0)
				return false;
			bool shareSaved = false;
			runDatabase([&](MatrixDatabase &database) {
				shareSaved = database.saveMegolmKeyShare(sessionId, member.id, deviceIt.key(), shareIndex);
			});
			if (!shareSaved)
				return false;
			deviceMessages.insert(deviceIt.key(), QJsonObject{
				{QStringLiteral("algorithm"), QStringLiteral("m.olm.v1.curve25519-aes-sha2")},
				{QStringLiteral("sender_key"), senderKey},
				{QStringLiteral("ciphertext"), QJsonObject{{recipientKey, QJsonObject{
					{QStringLiteral("body"), QString::fromUtf8(ciphertext)},
					{QStringLiteral("type"), messageType}}}}}});
		}
		users.insert(member.id, deviceMessages);
	}
	if (users.isEmpty())
		return true;
	int targetDeviceCount = 0;
	for (auto userIt = users.constBegin(); userIt != users.constEnd(); ++userIt)
		targetDeviceCount += userIt.value().toObject().size();
	qWarning() << "[Matrix-E2EE] distributing outbound room key:"
		<< "room:" << roomId << "session:" << sessionId
		<< "targetUsers:" << users.size() << "targetDevices:" << targetDeviceCount;
	const QString txnId = generateTransactionId();
	const QJsonObject body{{QStringLiteral("messages"), users}};
	QNetworkRequest request(QUrl(constructUrl(QStringLiteral("/_matrix/client/v3/sendToDevice/m.room.encrypted/%1")
		.arg(QUrl::toPercentEncoding(txnId)))));
	request.setRawHeader("Authorization", QByteArray("Bearer ") + FAccesToken.toUtf8());
	request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
	QNetworkReply *reply = FNetworkAccessManager->put(request,
		QJsonDocument(body).toJson(QJsonDocument::Compact));
	reply->setProperty("requestType", QStringLiteral("to_device"));
	QEventLoop eventLoop;
	QObject::connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
	eventLoop.exec();
	const bool delivered = reply->error() == QNetworkReply::NoError &&
		reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() >= 200 &&
		reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() < 300;
	reply->deleteLater();
	return delivered;
}

void MatrixNetwork::onReplyError(QNetworkReply *reply)
{
	Q_UNUSED(reply);
	
	// This is a fallback error handler for all reply types
	// The actual errors are handled in the specific_*Finished methods above
}

