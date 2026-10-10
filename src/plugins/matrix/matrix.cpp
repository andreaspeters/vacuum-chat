#include "matrix.h"
#include "matrixavatarkeypolicy.h"
#include "matrixsessionpolicy.h"
#include "matrixnetwork.h"
#include "matrixroominvite.h"
#include "matrixdatabase.h"
#include "matrixverificationdialog.h"
#include "matrixjoinroomchatdialog.h"
#include "matrixcontext.h"
#include "matrixtextmessage.h"
#include <interfaces/matrixreply.h>
#include <utils/matrixhtml.h>
#include <utils/action.h>
#include <utils/menu.h>
#include <interfaces/ipresence.h>
#include <interfaces/imessagewidgets.h>
#include <definitions/actiongroups.h>
#include <definitions/rosterindextyperole.h>

#include <QByteArray>
#include <QDebug>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QFileDialog>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QElapsedTimer>
#include <QTimer>
#include <QPointer>

#include <QCryptographicHash>
#include <QStandardPaths>
#include <QLabel>
#include <QProgressBar>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QRegularExpression>
#include <QHash>
#include <QVariant>
#include <algorithm>

static QString matrixAvatarCachePath(const QString &profileDirectory, const QString &mxcUrl);


Matrix::Matrix()
	: FUuid("{5f9b0e2a-6c1e-4c93-9e2d-7e8b0f3d6a41}"), FMatrixNetwork(nullptr), FAccountManager(nullptr), FOptionsManager(nullptr), FMatrixAccount(nullptr), FShow(0), FInitialSyncWindow(nullptr)
{
	FMetadataRefreshClock.start();
}

QString Matrix::streamId() const
{
	return FStreamId;
}

QString Matrix::formatEmoticonForSending(const QString &iconKey) const
{
	if (!FEmoticons)
		return iconKey;
	const QString emoji = FEmoticons->unicodeByKey(iconKey);
	return emoji.isEmpty() ? iconKey : emoji;
}

QString Matrix::formatEmoticonsForSending(const QString &text) const
{
	QString result = text;
	static const QStringList keys = {
		QStringLiteral("smile"), QStringLiteral("lol"), QStringLiteral("rofl"),
		QStringLiteral("happy"), QStringLiteral("wink"), QStringLiteral("sad"),
		QStringLiteral("cry"), QStringLiteral("angry"), QStringLiteral("kiss"),
		QStringLiteral("heart"), QStringLiteral("ok"), QStringLiteral("yes"),
		QStringLiteral("no"), QStringLiteral("clapping"), QStringLiteral("angel"),
		QStringLiteral("diablo"), QStringLiteral("rolleyes"), QStringLiteral("drink"),
		QStringLiteral("music"), QStringLiteral("rose"), QStringLiteral("bomb"),
		QStringLiteral("mail"), QStringLiteral("bye"), QStringLiteral("greeting"),
		QStringLiteral("dance") };
	for (const QString &key : keys) {
		const QString emoji = formatEmoticonForSending(key);
		if (emoji == key)
			continue;
		result.replace(QStringLiteral(":%1:").arg(key), emoji);
		result.replace(QStringLiteral("*%1*").arg(key), emoji);
	}
	return result;
}

QString Matrix::formatEmoticonsForDisplay(const QString &text) const
{
	if (!FEmoticons)
		return text;
	return formatEmoticonsForSending(text);
}

QString Matrix::emojiPickerType() const
{
	if (!FMatrixAccount)
		return QString();
	QString pack = FMatrixAccount->optionsNode().value(QStringLiteral("matrix.emoji-pack")).toString();
	if (pack.isEmpty()) {
		const QString legacyFormat = FMatrixAccount->optionsNode()
			.value(QStringLiteral("matrix.emoji-format")).toString();
		pack = legacyFormat == QStringLiteral("xmpp")
			? QStringLiteral("text") : QStringLiteral("unicode");
	}
	return pack;
}

bool Matrix::setPresence(int AShow, const QString &AStatus)
{
	if (!FMatrixNetwork || !FMatrixAccount || !FMatrixAccount->isActive())
		return false;

	const MatrixSessionPolicy::PresenceAction action =
		MatrixSessionPolicy::actionForPresence(AShow, FNetworkLoggedIn);
	if (action == MatrixSessionPolicy::PresenceAction::Disconnect)
	{
		++FLoginGeneration;
		FLoginRequested = false;
		FLoginAccountId.clear();
		FLoginUserId.clear();
		FPendingShow = IPresence::Offline;
		FPendingStatus.clear();
		FNetworkLoggedIn = false;
		FNetworkInitialSyncComplete = false;
		FNetworkUserId.clear();
		const bool invoked = QMetaObject::invokeMethod(FMatrixNetwork, "logout", Qt::QueuedConnection);
		FShow = AShow;
		FStatus = AStatus;
		if (!FStreamId.isEmpty())
			emit protocolPresenceChanged(FStreamId, FShow, FStatus);
		return invoked;
	}
	if (action == MatrixSessionPolicy::PresenceAction::Connect)
	{
		const OptionsNode options = FMatrixAccount->optionsNode();
		const QString username = options.value("matrix.username").toString();
		if (username.isEmpty())
			return false;

		const QString currentAccountId = accountId();
		if (FLoginRequested)
		{
			if (FLoginAccountId != currentAccountId || FLoginUserId != username)
				return false;
			FPendingShow = AShow;
			FPendingStatus = AStatus;
			return true;
		}

		FPendingShow = AShow;
		FPendingStatus = AStatus;
		FLoginAccountId = currentAccountId;
		FLoginUserId = username;
		FLoginRequested = true;
		const quint64 sessionGeneration = ++FLoginGeneration;

		const QString deviceId = options.value("matrix.device-id").toString();
		const QString accessToken = qEnvironmentVariable("MATRIX_ACCESS_TOKEN");
		bool invoked = false;
		if (!accessToken.isEmpty() && qEnvironmentVariable("MATRIX_USER_ID") == username)
		{
			invoked = QMetaObject::invokeMethod(FMatrixNetwork, "loginWithAccessTokenForSession",
				Qt::QueuedConnection, Q_ARG(QString, username), Q_ARG(QString, accessToken),
				Q_ARG(QString, deviceId), Q_ARG(quint64, sessionGeneration));
		}
		else
		{
			const QString password = FMatrixAccount->password();
			if (password.isEmpty())
			{
				FLoginRequested = false;
				FLoginAccountId.clear();
				FLoginUserId.clear();
				FPendingStatus.clear();
				return false;
			}
			invoked = QMetaObject::invokeMethod(FMatrixNetwork, "loginForSession", Qt::QueuedConnection,
				Q_ARG(QString, username), Q_ARG(QString, password), Q_ARG(QString, deviceId),
				Q_ARG(quint64, sessionGeneration));
		}
		if (!invoked)
		{
			FLoginRequested = false;
			FLoginAccountId.clear();
			FLoginUserId.clear();
			FPendingStatus.clear();
			return false;
		}
		return true;
	}
	if (FNetworkUserId.isEmpty())
		return false;

	QString presence = QStringLiteral("online");
	if (AShow == IPresence::Offline || AShow == IPresence::Error)
		presence = QStringLiteral("offline");
	else if (AShow == IPresence::Away || AShow == IPresence::ExtendedAway ||
		AShow == IPresence::DoNotDisturb)
		presence = QStringLiteral("unavailable");
	QMetaObject::invokeMethod(FMatrixNetwork, "setPresence", Qt::QueuedConnection,
		Q_ARG(QString, FNetworkUserId), Q_ARG(QString, presence), Q_ARG(QString, AStatus));
	FShow = AShow;
	FStatus = AStatus;
	emit protocolPresenceChanged(FStreamId, FShow, FStatus);
	return true;
}

QString Matrix::accountId() const
{
	return FMatrixAccount ? FMatrixAccount->accountId().toString() : QString();
}

ProtocolAccountIdentifier Matrix::accountIdentifier() const
{
	return FNetworkUserId.isEmpty()
		? ProtocolAccountIdentifier()
		: ProtocolAccountIdentifier{tr("Matrix user ID"), FNetworkUserId};
}

void Matrix::requestCurrentSync(const QString &accountId)
{
	if (!FMatrixNetwork || !FNetworkLoggedIn || accountId != this->accountId())
		return;
	QMetaObject::invokeMethod(FMatrixNetwork, "sync", Qt::QueuedConnection,
		Q_ARG(bool, true));
}

void Matrix::requestSsssRecoveryForAccount(const QString &accountId)
{
	if (!FMatrixNetwork || !FNetworkLoggedIn || accountId != this->accountId())
		return;
	if (FSsssDialog) {
		FSsssDialog->raise();
		FSsssDialog->activateWindow();
		return;
	}
	FSsssDialog = new MatrixSsssDialog();
	connect(FSsssDialog, &MatrixSsssDialog::recoverySubmitted, this,
		[this](const QString &value) {
			QMetaObject::invokeMethod(FMatrixNetwork, "requestSsssRecovery", Qt::QueuedConnection,
				Q_ARG(QString, value));
		});
	connect(FMatrixNetwork, &MatrixNetwork::ssssRecoveryFinished, FSsssDialog,
		[dialog = FSsssDialog](bool success, const QString &error) {
			dialog->setResult(success, error);
		});
	connect(FSsssDialog, &QDialog::finished, this, [this]() {
			FSsssDialog = nullptr;
		});
	FSsssDialog->show();
}

void Matrix::requestRoomKeyImportForAccount(const QString &accountId)
{
	if (!FMatrixNetwork || !FNetworkLoggedIn || accountId != this->accountId())
		return;
	const QString filePath = QFileDialog::getOpenFileName(nullptr,
		tr("Import Matrix room keys"), QString(),
		tr("Matrix Megolm session export (*.txt *.keys);;All files (*)"));
	if (filePath.isEmpty())
		return;
	bool ok = false;
	const QString passphrase = QInputDialog::getText(nullptr,
		tr("Room-key export password"),
		tr("Enter the password used for the Element/Nheko room-key export:"),
		QLineEdit::Password, QString(), &ok);
	if (!ok || passphrase.isEmpty())
		return;
	bool imported = false;
	QString error;
	QMetaObject::invokeMethod(FMatrixNetwork, "importRoomKeyFile", Qt::BlockingQueuedConnection,
		Q_RETURN_ARG(bool, imported), Q_ARG(QString, filePath),
		Q_ARG(QString, passphrase), Q_ARG(QString &, error));
	if (!imported)
		QMessageBox::warning(nullptr, tr("Matrix room-key import"), error);
	else
		QMessageBox::information(nullptr, tr("Matrix room-key import"),
			tr("The room keys were imported successfully."));
}

bool Matrix::requestDeviceVerification(const QString &transactionId, const QString &userId,
	const QString &deviceId)
{
	if (!FMatrixNetwork)
		return false;
	showVerificationDialog(transactionId, userId, deviceId);
	return QMetaObject::invokeMethod(FMatrixNetwork, "requestSasVerification", Qt::QueuedConnection,
		Q_ARG(QString, transactionId), Q_ARG(QString, userId), Q_ARG(QString, deviceId));
}

void Matrix::requestDeviceVerificationForAccount(const QString &accountId,
	const QString &userId, const QString &deviceId)
{
	if (!FMatrixAccount || FMatrixAccount->accountId().toString() != accountId ||
		userId.isEmpty() || deviceId.isEmpty())
		return;
	if (FMatrixNetwork && userId == FNetworkUserId && deviceId == FNetworkDeviceId) {
		qWarning() << "[Matrix-E2EE] refusing verification request to the local Vacuum device:"
			<< userId << deviceId;
		return;
	}
	requestDeviceVerification(QUuid::createUuid().toString(QUuid::WithoutBraces), userId, deviceId);
}

bool Matrix::cancelDeviceVerification(const QString &transactionId, const QString &userId,
	const QString &deviceId)
{
	if (!FMatrixNetwork)
		return false;
	return QMetaObject::invokeMethod(FMatrixNetwork, "cancelSasVerification", Qt::QueuedConnection,
		Q_ARG(QString, transactionId), Q_ARG(QString, userId), Q_ARG(QString, deviceId));
}

bool Matrix::acceptDeviceVerification(const QString &transactionId, const QString &userId,
	const QString &deviceId)
{
	if (!FMatrixNetwork)
		return false;
	return QMetaObject::invokeMethod(FMatrixNetwork, "acceptSasVerification", Qt::QueuedConnection,
		Q_ARG(QString, transactionId), Q_ARG(QString, userId), Q_ARG(QString, deviceId));
}

bool Matrix::startDeviceVerification(const QString &transactionId, const QString &userId,
	const QString &deviceId)
{
	if (!FMatrixNetwork)
		return false;
	return QMetaObject::invokeMethod(FMatrixNetwork, "startSasVerification", Qt::QueuedConnection,
		Q_ARG(QString, transactionId), Q_ARG(QString, userId), Q_ARG(QString, deviceId));
}

bool Matrix::confirmDeviceVerification(const QString &transactionId, const QString &userId,
	const QString &deviceId)
{
	qWarning() << "[Matrix-E2EE] UI confirm requested:" << transactionId
		<< "user:" << userId << "device:" << deviceId
		<< "network:" << (FMatrixNetwork != nullptr);
	if (!FMatrixNetwork)
		return false;
	const bool queued = QMetaObject::invokeMethod(FMatrixNetwork, "confirmSasVerification", Qt::QueuedConnection,
		Q_ARG(QString, transactionId), Q_ARG(QString, userId), Q_ARG(QString, deviceId));
	qWarning() << "[Matrix-E2EE] UI confirm queued:" << transactionId << queued;
	return queued;
}

void Matrix::showVerificationDialog(const QString &transactionId, const QString &userId,
	const QString &deviceId)
{
	if (transactionId.isEmpty() || userId.isEmpty() || deviceId.isEmpty())
		return;
	if (FVerificationDialog && FVerificationDialog->transactionId() == transactionId) {
		FVerificationDialog->show();
		FVerificationDialog->raise();
		FVerificationDialog->activateWindow();
		return;
	}
	if (FVerificationDialog)
		FVerificationDialog->deleteLater();
	FVerificationDialog = new MatrixVerificationDialog(transactionId, userId, deviceId);
	connect(FVerificationDialog, &MatrixVerificationDialog::acceptRequested, this,
		[this](const QString &tx, const QString &user, const QString &device) {
			acceptDeviceVerification(tx, user, device);
		});
	connect(FVerificationDialog, &MatrixVerificationDialog::startRequested, this,
		[this](const QString &tx, const QString &user, const QString &device) {
			startDeviceVerification(tx, user, device);
		});
	connect(FVerificationDialog, &MatrixVerificationDialog::confirmRequested, this,
		[this](const QString &tx, const QString &user, const QString &device) {
			confirmDeviceVerification(tx, user, device);
		});
	connect(FVerificationDialog, &MatrixVerificationDialog::cancelRequested, this,
		[this](const QString &tx, const QString &user, const QString &device) {
			cancelDeviceVerification(tx, user, device);
		});
	connect(FVerificationDialog, &QObject::destroyed, this, [this]() {
		FVerificationDialog = nullptr;
	});
	connect(FVerificationDialog, &QDialog::finished, FVerificationDialog, &QObject::deleteLater);
	FVerificationDialog->show();
	FVerificationDialog->raise();
	FVerificationDialog->activateWindow();
}

void Matrix::onVerificationRequest(const QString &transactionId, const QString &userId,
	const QString &deviceId)
{
	if (!FNetworkInitialSyncComplete) {
		FDeferredVerificationTransactionId = transactionId;
		FDeferredVerificationUserId = userId;
		FDeferredVerificationDeviceId = deviceId;
		qWarning() << "Matrix verification request deferred until initial sync completes";
		return;
	}
	ProtocolNotification notification;
	notification.id = QStringLiteral("verification:") + transactionId;
	notification.accountId = accountId();
	notification.title = userId;
	notification.body = tr("Matrix device verification requested");
	notification.protocol = protocol();
	notification.timestamp = QDateTime::currentDateTimeUtc();
	notification.kind = ProtocolNotification::Verification;
	appendNotification(notification);
	showVerificationDialog(transactionId, userId, deviceId);
}

void Matrix::onVerificationSas(const QString &transactionId, const QStringList &emoji,
	const QString &decimal)
{
	qWarning() << "[Matrix-E2EE] SAS received by UI:" << transactionId
		<< "emojiCount:" << emoji.size()
		<< "dialogMatch:" << (FVerificationDialog &&
			FVerificationDialog->transactionId() == transactionId)
		<< "initialSyncComplete:" << FNetworkInitialSyncComplete;
	if (!FNetworkInitialSyncComplete) {
		FDeferredVerificationSasTransactionId = transactionId;
		FDeferredVerificationSasEmoji = emoji;
		FDeferredVerificationSasDecimal = decimal;
		return;
	}
	if (FVerificationDialog && FVerificationDialog->transactionId() == transactionId)
		FVerificationDialog->setSas(emoji, decimal);
}

void Matrix::onVerificationState(const QString &transactionId, const QString &state)
{
	if (!FNetworkInitialSyncComplete) {
		FDeferredVerificationStateTransactionId = transactionId;
		FDeferredVerificationState = state;
		return;
	}
	if (state == QStringLiteral("verified") && FMatrixNetwork)
		QMetaObject::invokeMethod(FMatrixNetwork, "refreshRosterSnapshot", Qt::QueuedConnection);
	if (FVerificationDialog && FVerificationDialog->transactionId() == transactionId) {
		FVerificationDialog->setState(state);
		if (state == QStringLiteral("verified") && FDatabaseWorker)
			QMetaObject::invokeMethod(FDatabaseWorker, "checkCompleteCrossSigningKeys",
				Qt::QueuedConnection, Q_ARG(QString, FVerificationDialog->userId()));
		if (state == QStringLiteral("verified") && FVerificationDialog->userId() != FNetworkUserId &&
			!FSsssUserSigningSecret.isEmpty())
			QMetaObject::invokeMethod(FMatrixNetwork, "uploadUserMasterSignature", Qt::QueuedConnection,
				Q_ARG(QString, FVerificationDialog->userId()),
				Q_ARG(QByteArray, FSsssUserSigningSecret));
	}
	if (state == QStringLiteral("verified") && FVerificationDialog &&
		FVerificationDialog->userId() == FNetworkUserId && FMatrixNetwork)
		QMetaObject::invokeMethod(FMatrixNetwork, "requestSsssRecoveryFromVerifiedDevices",
			Qt::QueuedConnection);
}

void Matrix::onCompleteCrossSigningKeysChecked(const QString &userId, bool complete)
{
	if (FVerificationDialog && FVerificationDialog->userId() == userId)
		FVerificationDialog->setCrossSigningStatus(complete);
}

void Matrix::onDeviceTrustChanged(const QString &userId, const QString &deviceId)
{
	// Device-key changes are emitted from the network/E2EE worker. Never open
	// a modal dialog here: one /keys/query response can contain many devices
	// and QMessageBox::exec() would block the UI once per device.
	qWarning() << "Matrix device trust invalidated:" << userId << deviceId;
	if (FMatrixNetwork)
		QMetaObject::invokeMethod(FMatrixNetwork, "refreshRosterSnapshot", Qt::QueuedConnection);
}

void Matrix::onSsssRecoveryFinished(bool success, const QString &error)
{
	FSsssMasterSecret.clear();
	FSsssSelfSigningSecret.clear();
	FSsssUserSigningSecret.clear();
	if (!success || !FMatrixNetwork) {
		if (!success)
			qWarning() << "[Matrix-E2EE] SSSS recovery failed:" << error;
		return;
	}
	QMetaObject::invokeMethod(FMatrixNetwork, "takeSsssSecret", Qt::BlockingQueuedConnection,
		Q_RETURN_ARG(QByteArray, FSsssMasterSecret),
		Q_ARG(QString, QStringLiteral("m.cross_signing.master")));
	QMetaObject::invokeMethod(FMatrixNetwork, "takeSsssSecret", Qt::BlockingQueuedConnection,
		Q_RETURN_ARG(QByteArray, FSsssSelfSigningSecret),
		Q_ARG(QString, QStringLiteral("m.cross_signing.self_signing")));
	QMetaObject::invokeMethod(FMatrixNetwork, "takeSsssSecret", Qt::BlockingQueuedConnection,
		Q_RETURN_ARG(QByteArray, FSsssUserSigningSecret),
		Q_ARG(QString, QStringLiteral("m.cross_signing.user_signing")));
	if (!FSsssSelfSigningSecret.isEmpty())
		QMetaObject::invokeMethod(FMatrixNetwork, "uploadOwnDeviceSignature", Qt::QueuedConnection,
			Q_ARG(QByteArray, FSsssSelfSigningSecret));
	QMetaObject::invokeMethod(FMatrixNetwork, "uploadRecoveredMasterKeySignature",
		Qt::QueuedConnection);
	qWarning() << "[Matrix-E2EE] SSSS recovery completed; private cross-signing keys held in RAM only";
}

ProtocolRosterEntry Matrix::entry(const QString &AId) const
{
	for (const ProtocolRosterEntry &entry : FProtocolEntries)
		if (entry.id == AId)
			return entry;
	return ProtocolRosterEntry();
}

QList<ProtocolRoom> Matrix::rooms() const
{
	QList<ProtocolRoom> visibleRooms;
	for (const ProtocolRoom &room : FProtocolRooms)
		if (room.roomType != QStringLiteral("m.space"))
			visibleRooms.append(room);
	return visibleRooms;
}

ProtocolRoom Matrix::room(const QString &AId) const
{
	for (const ProtocolRoom &room : FProtocolRooms)
		if (room.id == AId)
			return room;
	return ProtocolRoom();
}

Matrix::~Matrix()
{
	auto stopThread = [](QThread *thread, QObject *object) {
		if (!thread)
			return;
		if (thread->isRunning() && object) {
			QMetaObject::invokeMethod(object, [object]() {
				if (auto *network = qobject_cast<MatrixNetwork *>(object))
					network->shutdown();
				delete object;
			},
				Qt::BlockingQueuedConnection);
		}
		else if (object) {
			delete object;
		}
		thread->quit();
		thread->wait();
		delete thread;
	};
	stopThread(FNetworkThread, FMatrixNetwork);
	FNetworkThread = nullptr;
	FMatrixNetwork = nullptr;
	stopThread(FDatabaseThread, FDatabaseWorker);
	FDatabaseThread = nullptr;
	FDatabaseWorker = nullptr;
	delete FInitialSyncWindow;
}

void Matrix::pluginInfo(IPluginInfo *APluginInfo)
{
	APluginInfo->name = "Matrix Client-Server";
	APluginInfo->description = "Matrix protocol support with C-S API";
	APluginInfo->version = "0.1.0";
	APluginInfo->author = "Andreas Peters";
	APluginInfo->homePage = QUrl("https://github.com/andreaspeters/vacuum-chat");
}

bool Matrix::initConnections(IPluginManager *APluginManager, int &AInitOrder)
{
	AInitOrder = 0;
	IPlugin *plugin = APluginManager->pluginInterface("IAccountManager").value(0, NULL);
	if (plugin) {
		IAccountManager *accounts = qobject_cast<IAccountManager *>(plugin->instance());
		if (accounts) {
			FAccountManager = accounts;
			connect(accounts->instance(), SIGNAL(shown(IAccount *)), this, SLOT(onAccountShown(IAccount *)));
			connect(accounts->instance(), SIGNAL(appended(IAccount *)), this, SLOT(onAccountAppended(IAccount *)));
			connect(accounts->instance(), SIGNAL(changed(IAccount *,OptionsNode)), this, SLOT(onAccountChanged(IAccount *,OptionsNode)));
			connect(accounts->instance(), SIGNAL(hidden(IAccount *)), this, SLOT(onAccountHidden(IAccount *)));

		}
	}
	plugin = APluginManager->pluginInterface("IRostersViewPlugin").value(0, NULL);
	if (plugin) {
		FRostersViewPlugin = qobject_cast<IRostersViewPlugin *>(plugin->instance());
	}
	plugin = APluginManager->pluginInterface("IMessageWidgets").value(0, NULL);
	if (plugin)
		FMessageWidgets = qobject_cast<IMessageWidgets *>(plugin->instance());
	plugin = APluginManager->pluginInterface("IOptionsManager").value(0, NULL);
	if (plugin)
		FOptionsManager = qobject_cast<IOptionsManager *>(plugin->instance());
	plugin = APluginManager->pluginInterface("IAvatars").value(0, NULL);
	if (plugin)
		FAvatars = qobject_cast<IAvatars *>(plugin->instance());
	plugin = APluginManager->pluginInterface("IEmoticons").value(0, NULL);
	if (plugin)
		FEmoticons = qobject_cast<IEmoticons *>(plugin->instance());
	return true;
}

IProtocolCapabilities::Capabilities Matrix::capabilitiesForAccount(
	const AccountId &accountId, const ConversationId &targetId) const
{
	if ((accountId != this->accountId() && accountId != streamId()) || !FMatrixAccount ||
		!FMatrixAccount->isActive() || !FMatrixAccount->isValid() || !FMatrixNetwork)
		return IProtocolCapabilities::Capabilities();
	IProtocolCapabilities::Capabilities capabilities(IProtocolCapabilities::CapabilitySetPresence);
	capabilities |= IProtocolCapabilities::CapabilitySendImage;
	if (!FNetworkLoggedIn)
		return capabilities;
	capabilities |= IProtocolCapabilities::CapabilityEditProfile;
	if (!FNetworkInitialSyncComplete)
		return capabilities;
	capabilities |= IProtocolCapabilities::CapabilityAddContact;
	capabilities |= IProtocolCapabilities::CapabilitySetPresence;
	capabilities |= IProtocolCapabilities::CapabilitySetAccountAvatar;
	if (!targetId.isEmpty())
		capabilities |= IProtocolCapabilities::CapabilityViewHistory;
	return capabilities;
}

bool Matrix::showAddContactDialog(const AccountId &accountId)
{
	if (!hasCapabilities(accountId, IProtocolCapabilities::CapabilityAddContact))
		return false;
	showRoomChatDialog(accountId);
	return true;
}

bool Matrix::setAccountAvatar(const AccountId &accountId, const QByteArray &imageData)
{
	if (!hasCapabilities(accountId, IProtocolCapabilities::CapabilitySetAccountAvatar))
		return false;
	return QMetaObject::invokeMethod(FMatrixNetwork, "setOwnAvatar", Qt::QueuedConnection,
		Q_ARG(QByteArray, imageData));
}

bool Matrix::showProfile(const AccountId &accountId, const UserId &userId)
{
	Q_UNUSED(accountId);
	Q_UNUSED(userId);
	return false;
}

bool Matrix::editProfile(const AccountId &accountId)
{
	if (!hasCapabilities(accountId, IProtocolCapabilities::CapabilityEditProfile) ||
		!FMatrixNetwork || FNetworkUserId.isEmpty())
		return false;
	const QString userId = FNetworkUserId;
	const quint64 loginGeneration = FLoginGeneration;
	QWidget *parent = FRostersViewPlugin && FRostersViewPlugin->rostersView()
		? FRostersViewPlugin->rostersView()->instance() : nullptr;
	bool accepted = false;
	const QString displayName = QInputDialog::getText(parent, tr("Edit Matrix profile"),
		tr("Display name for %1 (leave empty to clear it):").arg(userId),
		QLineEdit::Normal, QString(), &accepted);
	if (!accepted || !FMatrixNetwork || FNetworkUserId != userId ||
		FLoginGeneration != loginGeneration ||
		!hasCapabilities(accountId, IProtocolCapabilities::CapabilityEditProfile))
		return false;
	return QMetaObject::invokeMethod(FMatrixNetwork, "setOwnDisplayName", Qt::QueuedConnection,
		Q_ARG(QString, displayName));
}

void Matrix::showRoomChatDialog(const QString &boundAccountId)
{
	if (!FMatrixNetwork || !FMatrixAccount ||
		FMatrixAccount->accountId().toString() != boundAccountId)
		return;
	QWidget *parent = FRostersViewPlugin && FRostersViewPlugin->rostersView()
		? FRostersViewPlugin->rostersView()->instance() : nullptr;
	MatrixJoinRoomChatDialog dialog(parent);
	connect(&dialog, &MatrixJoinRoomChatDialog::publicRoomSearchRequested,
		FMatrixNetwork, &MatrixNetwork::searchPublicRooms);
	connect(FMatrixNetwork, &MatrixNetwork::publicRoomsReceived,
		&dialog, &MatrixJoinRoomChatDialog::setPublicRoomsResult);
	connect(&dialog, &MatrixJoinRoomChatDialog::joinRoomRequested,
		FMatrixNetwork, &MatrixNetwork::joinRoom);
	connect(&dialog, &MatrixJoinRoomChatDialog::startDirectChatRequested,
		FMatrixNetwork, &MatrixNetwork::startDirectChat);
	connect(FMatrixNetwork, &MatrixNetwork::directRoomCreated,
		&dialog, &MatrixJoinRoomChatDialog::setDirectRoomCreated);
	connect(&dialog, &MatrixJoinRoomChatDialog::conversationReady, this,
		[this, boundAccountId](const QString &roomId) {
			if (!FMatrixAccount || FMatrixAccount->accountId().toString() != boundAccountId)
				return;
			setActiveConversation(roomId);
			if (FMessageWidgets) {
				IChatWindow *window = FMessageWidgets->getConversationWindow(boundAccountId, roomId);
				if (window)
					window->showTabPage();
			}
		});
	dialog.exec();
}

bool Matrix::initObjects()
{
	FMatrixNetwork = new MatrixNetwork();
	FNetworkThread = new QThread();
	FNetworkThread->setObjectName(QStringLiteral("MatrixNetworkThread"));
	FMatrixNetwork->moveToThread(FNetworkThread);
	FNetworkThread->start();
	qRegisterMetaType<QList<MatrixCachedRoom>>();
	qRegisterMetaType<QList<MatrixTimelineEvent>>();
	qRegisterMetaType<MatrixHistoryPageResult>();
	qRegisterMetaType<MatrixNotificationEvent>();
	FDatabaseThread = new QThread();
	FDatabaseThread->setObjectName(QStringLiteral("MatrixDatabaseThread"));
	FDatabaseWorker = new MatrixDatabaseWorker();
	FDatabaseWorker->moveToThread(FDatabaseThread);
	connect(FDatabaseWorker, &MatrixDatabaseWorker::roomsLoaded,
		this, &Matrix::onCachedRoomsLoaded, Qt::QueuedConnection);
	connect(FDatabaseWorker, &MatrixDatabaseWorker::loadFailed,
		this, &Matrix::onCachedRoomsLoadFailed, Qt::QueuedConnection);
	connect(FDatabaseWorker, &MatrixDatabaseWorker::historyLoaded,
		this, &Matrix::onCachedHistoryLoaded, Qt::QueuedConnection);
	connect(FDatabaseWorker, &MatrixDatabaseWorker::historyLoadFailed,
		this, &Matrix::onCachedHistoryLoadFailed, Qt::QueuedConnection);
	connect(FDatabaseWorker, &MatrixDatabaseWorker::historyPageLoaded,
		this, &Matrix::onHistoryPageLoaded, Qt::QueuedConnection);
	connect(FDatabaseWorker, &MatrixDatabaseWorker::completeCrossSigningKeysChecked,
		this, &Matrix::onCompleteCrossSigningKeysChecked, Qt::QueuedConnection);
	FDatabaseThread->start();
	QMetaObject::invokeMethod(FMatrixNetwork, "setDatabaseWorker", Qt::QueuedConnection,
		Q_ARG(QObject *, FDatabaseWorker));
	
	return true;
}

bool Matrix::initSettings()
{
	return true;
}

bool Matrix::startPlugin()
{
	if (FAccountManager) {
		const QString accessToken = qEnvironmentVariable("MATRIX_ACCESS_TOKEN");
		const QString userId = qEnvironmentVariable("MATRIX_USER_ID");
		const QString serverUrl = qEnvironmentVariable("MATRIX_HOMESERVER");
		bool hasMatrixAccount = false;
		for (IAccount *account : FAccountManager->accounts())
			if (account && account->optionsNode().value("type").toString() == QStringLiteral("matrix"))
				hasMatrixAccount = true;
		if (!hasMatrixAccount && !accessToken.isEmpty() && !userId.isEmpty() && !serverUrl.isEmpty()) {
			IAccount *account = FAccountManager->appendAccount(QUuid::createUuid());
			if (account) {
				OptionsNode options = account->optionsNode();
				options.setValue(QStringLiteral("matrix"), "type");
				options.setValue(serverUrl, "matrix.instance");
				options.setValue(userId, "matrix.username");
				options.setValue(qEnvironmentVariable("MATRIX_DEVICE_ID"), "matrix.device-id");
				account->setName(QStringLiteral("Matrix live smoke"));
				account->setActive(true);
			}
		}
		foreach (IAccount *account, FAccountManager->accounts()) {
			if (account->isActive())
				onAccountShown(account);
		}
	}
	return true;
}

void Matrix::onAccountShown(IAccount *AAccount)
{
	if (!AAccount || !FMatrixNetwork)
	{
		return;
	}
	if (FMatrixAccount == AAccount && (FNetworkLoggedIn || FNetworkUserId ==
		AAccount->optionsNode().value("matrix.username").toString()))
	{
		return;
	}

	OptionsNode options = AAccount->optionsNode();
	QString accountType = options.value("type").toString();
	if (accountType != QStringLiteral("matrix"))
	{
		return;
	}

	FMatrixAccount = AAccount;
	QString serverUrl = options.value("matrix.instance").toString();
	QString username = options.value("matrix.username").toString();
	QString deviceId = options.value("matrix.device-id").toString();
	const QString host = QUrl(serverUrl).host();
	QString localUser = username;
	if (localUser.startsWith('@')) localUser.remove(0, 1);
	const int colon = localUser.indexOf(':');
	if (colon >= 0) localUser.truncate(colon);
	FStreamId = localUser + "@" + host;
	const QString profileDirectory = FOptionsManager && !FOptionsManager->currentProfile().isEmpty()
		? FOptionsManager->profilePath(FOptionsManager->currentProfile()) : QString();
	QMetaObject::invokeMethod(FMatrixNetwork, "setDatabaseProfileDirectory", Qt::QueuedConnection,
		Q_ARG(QString, profileDirectory));
	QMetaObject::invokeMethod(FMatrixNetwork, "setServerUrl", Qt::QueuedConnection,
		Q_ARG(QString, serverUrl));
	FDatabaseProfileDirectory = profileDirectory;
	FDatabaseServerUrl = serverUrl;
	FDatabaseUserId = username;
	FNetworkDeviceId = deviceId;

	connect(FMatrixNetwork, SIGNAL(loginSuccessForSession(QString,QString,QString,quint64)),
		this, SLOT(onLoginSuccess(QString,QString,QString,quint64)), Qt::UniqueConnection);
	connect(FMatrixNetwork, SIGNAL(loginErrorForSession(QString,quint64)),
		this, SLOT(onLoginError(QString,quint64)), Qt::UniqueConnection);
	connect(FMatrixNetwork, SIGNAL(syncError(QString)), this, SLOT(onSyncError(QString)), Qt::UniqueConnection);
	connect(FMatrixNetwork, SIGNAL(rosterChanged(QList<ProtocolRoom>)),
		this, SLOT(onRosterChanged(QList<ProtocolRoom>)), Qt::UniqueConnection);
	connect(FMatrixNetwork, &MatrixNetwork::roomMetadataChanged, this,
		&Matrix::onRoomMetadataChanged, Qt::UniqueConnection);
	connect(FMatrixNetwork, SIGNAL(messageReceived(BasicMessage)),
		this, SLOT(onNetworkMessageReceived(BasicMessage)), Qt::UniqueConnection);
	connect(FMatrixNetwork, &MatrixNetwork::typingChanged,
		this, &Matrix::onNetworkTypingChanged, Qt::UniqueConnection);
	connect(FMatrixNetwork, &MatrixNetwork::notificationEventReceived,
		this, &Matrix::onNetworkNotificationEvent, Qt::UniqueConnection);
	connect(FMatrixNetwork, &MatrixNetwork::avatarImageReceived, this,
		&Matrix::onAvatarImageReceived, Qt::UniqueConnection);
	connect(FMatrixNetwork, &MatrixNetwork::accountAvatarUpdateFinished, this,
		&Matrix::onAccountAvatarUpdateFinished, Qt::UniqueConnection);
	connect(FMatrixNetwork, &MatrixNetwork::accountDisplayNameUpdateFinished, this,
		&Matrix::onAccountDisplayNameUpdateFinished, Qt::UniqueConnection);
	connect(FMatrixNetwork, &MatrixNetwork::displayNameReceived, this,
		&Matrix::onDisplayNameReceived, Qt::UniqueConnection);
	connect(FMatrixNetwork, &MatrixNetwork::roomNameReceived, this,
		&Matrix::onRoomNameReceived, Qt::UniqueConnection);
	connect(FMatrixNetwork, SIGNAL(syncReceived(QList<MatrixTextEvent>)),
		this, SLOT(onSyncReceived(QList<MatrixTextEvent>)), Qt::UniqueConnection);
	connect(FMatrixNetwork, SIGNAL(messageHistoryChanged(QString,QList<MatrixTextEvent>)),
		this, SLOT(onMessageHistoryChanged(QString,QList<MatrixTextEvent>)), Qt::UniqueConnection);
	connect(FMatrixNetwork, &MatrixNetwork::initialSyncCompleted, this,
		&Matrix::onInitialSyncCompleted, Qt::UniqueConnection);
	connect(FMatrixNetwork, &MatrixNetwork::verificationStateChanged, this,
		&Matrix::protocolVerificationStateChanged, Qt::UniqueConnection);
	connect(FMatrixNetwork, &MatrixNetwork::verificationSasAvailable, this,
		&Matrix::protocolVerificationSasAvailable, Qt::UniqueConnection);
	connect(FMatrixNetwork, &MatrixNetwork::verificationRequestReceived, this,
		&Matrix::onVerificationRequest, Qt::UniqueConnection);
	connect(FMatrixNetwork, &MatrixNetwork::verificationSasAvailable, this,
		&Matrix::onVerificationSas, Qt::UniqueConnection);
	connect(FMatrixNetwork, &MatrixNetwork::verificationStateChanged, this,
		&Matrix::onVerificationState, Qt::UniqueConnection);
	connect(FMatrixNetwork, &MatrixNetwork::deviceTrustChanged, this,
		&Matrix::onDeviceTrustChanged, Qt::UniqueConnection);
	connect(FMatrixNetwork, &MatrixNetwork::ssssRecoveryFinished, this,
		&Matrix::onSsssRecoveryFinished, Qt::UniqueConnection);
	connect(FMatrixNetwork, SIGNAL(messageDeliveryChanged(QString,QString,QString,QString)),
		this, SIGNAL(protocolMessageDeliveryChanged(QString,QString,QString,QString)), Qt::UniqueConnection);
	connect(FMatrixNetwork, &MatrixNetwork::receiptReceived, this,
		&Matrix::onNetworkReceiptReceived, Qt::UniqueConnection);

	// Publish initial Offline presence to allow StatusChanger to enable the explicit Connect action.
	// Do not login or start sync merely because the account was shown.
	FShow = IPresence::Offline;
	FStatus = QStringLiteral("Offline");
	emit protocolPresenceChanged(FStreamId, FShow, FStatus);

	// SQLite cache is available offline; loading it must not depend on login or /sync.
	loadRoomsFromDatabase();
}

void Matrix::onAccountAppended(IAccount *AAccount)
{
	if (AAccount && AAccount->isActive())
		onAccountShown(AAccount);
}

void Matrix::onAccountChanged(IAccount *AAccount, const OptionsNode &ANode)
{
	Q_UNUSED(AAccount);
	Q_UNUSED(ANode);
}

void Matrix::onAccountHidden(IAccount *AAccount)
{
	if (AAccount && AAccount == FMatrixAccount &&
		AAccount->optionsNode().value("type").toString() == QStringLiteral("matrix") && FMatrixNetwork)
	{
		if (!FStreamId.isEmpty())
			emit protocolPresenceClosed(FStreamId);
		FStreamId.clear();
		FShow = 0;
		FStatus.clear();
		FNetworkLoggedIn = false;
		FNetworkInitialSyncComplete = false;
		++FLoginGeneration;
		FLoginRequested = false;
		FLoginAccountId.clear();
		FLoginUserId.clear();
		FPendingShow = IPresence::Offline;
		FPendingStatus.clear();
		FNetworkUserId.clear();
		FProtocolRooms.clear();
		FProtocolEntries.clear();
		QMetaObject::invokeMethod(FMatrixNetwork, "logout", Qt::QueuedConnection);
		if (FInitialSyncWindow)
			FInitialSyncWindow->close();
		FMatrixAccount = nullptr;
		emit protocolRosterChanged();
	}
}

void Matrix::loadRoomsFromDatabase()
{
	if (!FDatabaseWorker || FDatabaseProfileDirectory.isEmpty() ||
		FDatabaseServerUrl.isEmpty() || FDatabaseUserId.isEmpty())
		return;
	QMetaObject::invokeMethod(FDatabaseWorker, "loadRooms", Qt::QueuedConnection,
		Q_ARG(QString, FDatabaseProfileDirectory), Q_ARG(QString, FDatabaseServerUrl),
		Q_ARG(QString, FDatabaseUserId));
}

void Matrix::onCachedRoomsLoaded(const QList<MatrixCachedRoom> &rooms)
{
	FProtocolRooms.clear();
	FProtocolEntries.clear();
	for (const MatrixCachedRoom &cached : rooms) {
		const MatrixStoredRoom &stored = cached.room;
		ProtocolRoom room;
		room.id = stored.roomId;
		room.name = stored.name.isEmpty() ? stored.roomId : stored.name;
		room.subject = stored.topic;
		room.roomType = stored.roomType;
		room.membership = stored.membership;
		room.isJoined = stored.membership == QStringLiteral("join");
		room.isDirect = stored.isDirect;
		room.isEncrypted = stored.isEncrypted;
		room.encryptionStateKnown = stored.encryptionStateKnown;
		room.isAvailable = true;
		room.avatarUrl = stored.avatarUrl;
		room.avatarKey = accountId() + QStringLiteral("\nroom\n") + room.id;
		for (const MatrixStoredMember &storedMember : cached.members) {
			ProtocolRosterEntry member;
			member.id = storedMember.userId;
			member.name = storedMember.displayName;
			member.avatarUrl = storedMember.avatarUrl;
			member.presence = QStringLiteral("unknown");
			member.isValid = true;
			room.members.append(member);
		}
		if (room.isDirect && room.avatarUrl.isEmpty())
			for (const ProtocolRosterEntry &member : room.members)
				if (member.id != FNetworkUserId && !member.avatarUrl.isEmpty()) {
					room.avatarUrl = member.avatarUrl;
					break;
				}
		FProtocolRooms.append(room);
	}
	QVariantMap knownRoomTypes;
	for (const ProtocolRoom &room : FProtocolRooms)
		if (!room.roomType.isEmpty())
			knownRoomTypes.insert(room.id, room.roomType);
	if (FMatrixNetwork && !knownRoomTypes.isEmpty())
		QMetaObject::invokeMethod(FMatrixNetwork, "setKnownRoomTypes", Qt::QueuedConnection,
			Q_ARG(QVariantMap, knownRoomTypes));
	emit protocolRosterChanged();
}

void Matrix::onCachedRoomsLoadFailed(const QString &error)
{
	qWarning() << "Matrix offline roster database load failed:" << error;
}

void Matrix::onCachedHistoryLoaded(const QString &roomId,
	const QList<MatrixTimelineEvent> &events)
{
	if (FMatrixNetwork && !events.isEmpty()) {
		MatrixNetwork *network = FMatrixNetwork;
		QMetaObject::invokeMethod(network, [network, roomId, events]() {
			network->requestHistoricalImages(roomId, events);
		}, Qt::QueuedConnection);
	}
	QList<BasicMessage> cachedMessages;
	for (const MatrixTimelineEvent &event : events) {
		BasicMessage message(event.eventId, roomId, event.sender, QString(), event.content,
			MatrixTimestamps::fromUnixMilliseconds(event.originTs), QStringLiteral("matrix"),
			BasicMessage::Incoming);
		QVariantMap metadata;
		for (auto it = event.metadata.constBegin(); it != event.metadata.constEnd(); ++it)
			metadata.insert(it.key(), it.value());
		metadata.insert(QStringLiteral("historical"), true);
		const bool senderIsSelf = FNetworkUserId.isEmpty()
			? (event.metadata.value(QStringLiteral("sender_is_self")).toBool() ||
				MatrixSessionPolicy::matchesLoginUserId(FDatabaseUserId, event.sender))
			: FNetworkUserId == event.sender;
		metadata.insert(QStringLiteral("sender_is_self"), senderIsSelf);
		message.setMetadata(metadata);
		cachedMessages.append(message);
	}
	const QList<BasicMessage> pendingMessages = FPendingHistoryMessages.take(roomId);
	const QList<BasicMessage> mergedMessages =
		mergeHistoryMessagesChronologically(cachedMessages, pendingMessages);
	if (!mergedMessages.isEmpty() && !FLatestConversationEventIds.contains(roomId))
		FLatestConversationEventIds.insert(roomId, mergedMessages.last().messageId());
	if (!mergedMessages.isEmpty()) {
		FCachedHistoryQueue.append(mergedMessages);
		FCachedHistoryRemainingByRoom[roomId] += mergedMessages.size();
	} else {
		finishHistoryLoad(roomId);
	}
	if (!FCachedHistoryBatchScheduled) {
		FCachedHistoryBatchScheduled = true;
		QTimer::singleShot(0, this, &Matrix::emitCachedHistoryBatch);
	}
}

void Matrix::onHistoryPageLoaded(const QString &roomId,
	const MatrixHistoryPageResult &result)
{
	if (!FPendingHistoryPageRequests.contains(roomId))
		return;
	const PendingHistoryPageRequest pending = FPendingHistoryPageRequests.take(roomId);
	ProtocolHistoryPage page;
	page.success = result.success;
	page.hasMore = result.hasMore;
	if (result.success) {
		for (const MatrixTimelineEvent &event : result.events) {
			BasicMessage message(event.eventId, roomId, event.sender, QString(), event.content,
				MatrixTimestamps::fromUnixMilliseconds(event.originTs), QStringLiteral("matrix"),
				BasicMessage::Incoming);
			QVariantMap metadata;
			for (auto it = event.metadata.constBegin(); it != event.metadata.constEnd(); ++it)
				metadata.insert(it.key(), it.value());
			metadata.insert(QStringLiteral("historical"), true);
			const bool senderIsSelf = FNetworkUserId.isEmpty()
				? (event.metadata.value(QStringLiteral("sender_is_self")).toBool() ||
					MatrixSessionPolicy::matchesLoginUserId(FDatabaseUserId, event.sender))
				: FNetworkUserId == event.sender;
			metadata.insert(QStringLiteral("sender_is_self"), senderIsSelf);
			message.setMetadata(metadata);
			if (!isDisplayedLocalEchoDuplicate(message))
				page.messages.append(message);
		}
		if (FMatrixNetwork && !result.events.isEmpty()) {
			MatrixNetwork *network = FMatrixNetwork;
			const QList<MatrixTimelineEvent> events = result.events;
			QMetaObject::invokeMethod(network, [network, roomId, events]() {
				network->requestHistoricalImages(roomId, events);
			}, Qt::QueuedConnection);
		}
	}
	if (!pending.callbackContext || !pending.callback)
		return;
	const QPointer<QObject> context = pending.callbackContext;
	const ProtocolHistoryPageCallback callback = pending.callback;
	QMetaObject::invokeMethod(context.data(), [context, callback, page]() {
		if (context)
			callback(page);
	}, Qt::QueuedConnection);
}

void Matrix::emitCachedHistoryBatch()
{
	constexpr qsizetype batchSize = 25;
	const qsizetype count = qMin(batchSize, FCachedHistoryQueue.size());
	for (qsizetype i = 0; i < count; ++i) {
		const BasicMessage message = FCachedHistoryQueue.takeFirst();
		if (!isDisplayedLocalEchoDuplicate(message))
			emit protocolMessageReceived(message);
		auto roomIt = FCachedHistoryRemainingByRoom.find(message.conversationId());
		if (roomIt != FCachedHistoryRemainingByRoom.end() && --roomIt.value() == 0) {
			const QString roomId = roomIt.key();
			FCachedHistoryRemainingByRoom.erase(roomIt);
			finishHistoryLoad(roomId);
		}
	}
	if (!FCachedHistoryQueue.isEmpty())
		QTimer::singleShot(1, this, &Matrix::emitCachedHistoryBatch);
	else
		FCachedHistoryBatchScheduled = false;
}

void Matrix::onLoginSuccess(const QString &AUserId, const QString &AAccessToken,
	const QString &ADeviceId, quint64 ASessionGeneration)
{
	if (ASessionGeneration != FLoginGeneration ||
		!MatrixSessionPolicy::acceptLoginSuccess(FLoginRequested))
		return;
	if (!FMatrixAccount || !FMatrixAccount->isActive() ||
		FLoginAccountId != accountId() ||
		!MatrixSessionPolicy::matchesLoginUserId(FLoginUserId, AUserId))
	{
		FLoginRequested = false;
		FLoginAccountId.clear();
		FLoginUserId.clear();
		FPendingStatus.clear();
		if (FMatrixNetwork)
			QMetaObject::invokeMethod(FMatrixNetwork, "logout", Qt::QueuedConnection);
		return;
	}
	const int requestedShow = FPendingShow;
	const QString requestedStatus = FPendingStatus;
	FLoginRequested = false;
	FLoginAccountId.clear();
	FLoginUserId.clear();
	FPendingStatus.clear();

	FNetworkUserId = AUserId;
	if (!ADeviceId.isEmpty())
		FNetworkDeviceId = ADeviceId;
	FAvatarLoadSeen.clear();
	FNetworkLoggedIn = true;
	FMetadataRefreshPolicy.clear();
	Q_UNUSED(AAccessToken);
	if (!FProtocolRooms.isEmpty())
		emit protocolRosterChanged();
	if (FInitialSyncWindow)
		FInitialSyncWindow->close();
	if (FMatrixAccount)
	{
		OptionsNode options = FMatrixAccount->optionsNode();
		QString user = options.value("matrix.username").toString();
		if (user.startsWith('@')) user.remove(0, 1);
		const int colon = user.indexOf(':');
		if (colon >= 0) user.truncate(colon);
		const QString host = QUrl(options.value("matrix.instance").toString()).host();
		FStreamId = user + "@" + host;
		FShow = requestedShow;
		FStatus = requestedStatus;
		emit protocolPresenceChanged(FStreamId, FShow, FStatus);
	}
	if (FMatrixNetwork)
	{
		// MatrixNetwork has opened and initialized the profile database already.
		// Only start the worker afterwards; otherwise a fresh database can be
		// initialized concurrently by two SQLite connections and block the UI
		// while the initial sync is starting.
		if (FDatabaseWorker)
			QMetaObject::invokeMethod(FDatabaseWorker, "loadRooms", Qt::QueuedConnection,
				Q_ARG(QString, FDatabaseProfileDirectory),
				Q_ARG(QString, FDatabaseServerUrl), Q_ARG(QString, FDatabaseUserId));
		FNotificationsReady = false;
		if (FMatrixAccount)
		{
			FMatrixAccount->optionsNode().setValue(FNetworkDeviceId, "matrix.device-id");
			// The device ID is part of the Olm account identity.  It must survive
			// a restart; otherwise the next login creates another Matrix device
			// and its OTK/account state can no longer decrypt queued PRE_KEY data.
			if (FOptionsManager && !FOptionsManager->saveOptions())
				qWarning() << "[Matrix-E2EE] failed to persist the server device ID";
		}
		QString presence = QStringLiteral("online");
		if (FShow == IPresence::Away || FShow == IPresence::ExtendedAway ||
			FShow == IPresence::DoNotDisturb)
			presence = QStringLiteral("unavailable");
		QMetaObject::invokeMethod(FMatrixNetwork, "setPresence", Qt::QueuedConnection,
			Q_ARG(QString, FNetworkUserId), Q_ARG(QString, presence), Q_ARG(QString, FStatus));
	QMetaObject::invokeMethod(FMatrixNetwork, "sync", Qt::QueuedConnection);
	}
}

void Matrix::onInitialSyncCompleted()
{
	FNetworkInitialSyncComplete = true;
	if (FInitialSyncWindow)
		FInitialSyncWindow->close();
	if (FDatabaseWorker)
		QMetaObject::invokeMethod(FDatabaseWorker, "loadRooms", Qt::QueuedConnection,
			Q_ARG(QString, FDatabaseProfileDirectory),
			Q_ARG(QString, FDatabaseServerUrl), Q_ARG(QString, FDatabaseUserId));
	if (!FDeferredVerificationTransactionId.isEmpty()) {
		const QString transactionId = FDeferredVerificationTransactionId;
		const QString userId = FDeferredVerificationUserId;
		const QString deviceId = FDeferredVerificationDeviceId;
		FDeferredVerificationTransactionId.clear();
		FDeferredVerificationUserId.clear();
		FDeferredVerificationDeviceId.clear();
		ProtocolNotification notification;
		notification.id = QStringLiteral("verification:") + transactionId;
		notification.accountId = accountId();
		notification.title = userId;
		notification.body = tr("Matrix device verification requested");
		notification.protocol = protocol();
		notification.timestamp = QDateTime::currentDateTimeUtc();
		notification.kind = ProtocolNotification::Verification;
		appendNotification(notification);
		QTimer::singleShot(0, this, [this, transactionId, userId, deviceId]() {
			showVerificationDialog(transactionId, userId, deviceId);
			if (FDeferredVerificationSasTransactionId == transactionId) {
				if (FVerificationDialog)
					FVerificationDialog->setSas(FDeferredVerificationSasEmoji,
						FDeferredVerificationSasDecimal);
				FDeferredVerificationSasTransactionId.clear();
				FDeferredVerificationSasEmoji.clear();
				FDeferredVerificationSasDecimal.clear();
			}
			if (FDeferredVerificationStateTransactionId == transactionId) {
				if (FVerificationDialog)
					FVerificationDialog->setState(FDeferredVerificationState);
				FDeferredVerificationStateTransactionId.clear();
				FDeferredVerificationState.clear();
			}
		});
	}
}

bool Matrix::sendMessage(const BasicMessage &message)
{
	if (!FMatrixNetwork || !FNetworkLoggedIn || message.conversationId().isEmpty())
		return false;
	const QString transactionId = QStringLiteral("m_") + QUuid::createUuid().toString(QUuid::WithoutBraces);
	QVariantMap metadata = message.metadata();
	metadata.insert(QStringLiteral("txn_id"), transactionId);
	metadata.insert(QStringLiteral("delivery_status"), QStringLiteral("sending"));
	const QString messageType = matrixMessageTypeForMetadata(metadata);
	metadata.insert(QStringLiteral("msgtype"), messageType);
	QString body = message.body();
	MatrixTextMessagePayload textPayload;
	if (messageType == QStringLiteral("m.text")) {
		textPayload = createMatrixTextMessagePayload(body, metadata);
		metadata = textPayload.localEchoMetadata;
	}
	BasicMessage localEcho(transactionId, message.conversationId(),
		FNetworkUserId, QString(), body, QDateTime::currentDateTimeUtc(),
		QStringLiteral("matrix"), BasicMessage::Outgoing);
	localEcho.setMetadata(metadata);
	FDisplayedLocalEchoTransactions.insert(message.conversationId() + QChar('\n') + transactionId);
	emit protocolMessageReceived(localEcho);
	if (messageType == QStringLiteral("m.text")) {
		QJsonObject content = textPayload.content;
		content = MatrixReply::applyRelation(content,
			metadata.value(QStringLiteral("reply_to_event_id")).toString());
		const QString roomId = message.conversationId();
		QMetaObject::invokeMethod(FMatrixNetwork,
			[this, roomId, content, transactionId]() {
				FMatrixNetwork->sendMessageEvent(roomId, content, transactionId);
			}, Qt::QueuedConnection);
		return true;
	}
	const QString filePath = metadata.value(QStringLiteral("file_path")).toString();
	if (!filePath.isEmpty()) {
		const QString mimeType = metadata.value(QStringLiteral("mimetype"),
			QStringLiteral("application/octet-stream")).toString();
		const QString roomId = message.conversationId();
		const QString body = message.body();
		QMetaObject::invokeMethod(FMatrixNetwork,
			[this, roomId, filePath, mimeType, messageType, body, transactionId]() {
				FMatrixNetwork->uploadFileAndSend(roomId, filePath, mimeType,
					messageType, body, transactionId);
			}, Qt::QueuedConnection);
		return true;
	}
	if (messageType == QStringLiteral("m.image")) {
		QJsonObject content;
		content.insert(QStringLiteral("msgtype"), messageType);
		content.insert(QStringLiteral("body"), message.body());
		const QString url = metadata.value(QStringLiteral("url"),
			metadata.value(QStringLiteral("image_url"))).toString();
		if (!url.isEmpty())
			content.insert(QStringLiteral("url"), url);
		const QString file = metadata.value(QStringLiteral("file")).toString();
		if (!file.isEmpty())
			content.insert(QStringLiteral("file"), file);
		const QString roomId = message.conversationId();
		QMetaObject::invokeMethod(FMatrixNetwork,
			[this, roomId, content, transactionId]() {
				FMatrixNetwork->sendMessageEvent(roomId, content, transactionId);
			}, Qt::QueuedConnection);
	} else {
		const QString roomId = message.conversationId();
		QMetaObject::invokeMethod(FMatrixNetwork,
			[this, roomId, body, transactionId]() {
				FMatrixNetwork->sendTextMessage(roomId, body, transactionId);
			}, Qt::QueuedConnection);
	}
	return true;
}

bool Matrix::supportsRoomInvites() const
{
	return FMatrixNetwork && FNetworkLoggedIn;
}

bool Matrix::inviteUserToRoom(const ConversationId &roomId, const UserId &userId)
{
	MatrixRoomInvite::Request inviteRequest;
	if (!supportsRoomInvites() || !MatrixRoomInvite::buildRequest(roomId, userId, inviteRequest))
		return false;

	QPointer<MatrixNetwork> network = FMatrixNetwork;
	return QMetaObject::invokeMethod(FMatrixNetwork,
		[network, roomId, userId]() {
			if (network)
				network->inviteUserToRoom(roomId, userId);
		}, Qt::QueuedConnection);
}

bool Matrix::supportsTyping(const ConversationId &conversationId) const
{
	return FMatrixNetwork && FNetworkLoggedIn && conversationId.startsWith(QLatin1Char('!'));
}

bool Matrix::supportsReactions(const ConversationId &conversationId) const
{
	return FMatrixNetwork && FNetworkLoggedIn && conversationId.startsWith(QLatin1Char('!'));
}

bool Matrix::sendReaction(const ConversationId &conversationId, const MessageId &eventId,
	const QString &key)
{
	if (!supportsReactions(conversationId) || !eventId.startsWith(QLatin1Char('$')) ||
		key.trimmed().isEmpty())
		return false;

	const QJsonObject relation{
		{QStringLiteral("rel_type"), QStringLiteral("m.annotation")},
		{QStringLiteral("event_id"), eventId},
		{QStringLiteral("key"), key}};
	const QJsonObject content{{QStringLiteral("m.relates_to"), relation}};
	const QString transactionId = QStringLiteral("m_") +
		QUuid::createUuid().toString(QUuid::WithoutBraces);
	MatrixNetwork *network = FMatrixNetwork;
	return QMetaObject::invokeMethod(network,
		[network, conversationId, content, transactionId]() {
			network->sendRoomEvent(conversationId, QStringLiteral("m.reaction"),
				content, transactionId);
		}, Qt::QueuedConnection);
}

bool Matrix::isEventIdLike(const QString &eventId) const
{
	return MatrixReply::isEventId(eventId);
}

QString Matrix::replyPreviewHtml(const QString &senderName, const QString &excerpt,
	const QString &avatarResourceUrl) const
{
	return MatrixReply::previewHtml(senderName, excerpt, avatarResourceUrl);
}

QString Matrix::stripReplyFallback(const QString &body, const QString &formatType) const
{
	if (formatType == QStringLiteral("text/html"))
		return MatrixReply::stripFallbackHtml(body);
	return MatrixReply::stripFallbackText(body);
}

QString Matrix::sanitizeHtml(const QString &html) const
{
	return ::matrixSafeHtml(html);
}

QString Matrix::highlightMentions(const QString &text) const
{
	return ::matrixHighlightMentions(text);
}

void Matrix::setTyping(const ConversationId &conversationId, ProtocolTypingStatus status)
{
	if (!supportsTyping(conversationId))
		return;
	const bool typing = status == TypingStatusComposing || status == TypingStatusPaused;
	QMetaObject::invokeMethod(FMatrixNetwork, "setTyping", Qt::QueuedConnection,
		Q_ARG(QString, conversationId), Q_ARG(bool, typing));
}

QList<BasicMessage> Matrix::conversationHistory(const QString &conversationId) const
{
	QList<BasicMessage> result;
	if (conversationId.isEmpty())
		return result;
	if (!FHistoryRequests.contains(conversationId) && FDatabaseWorker) {
		FHistoryRequests.insert(conversationId);
		FHistoryLoadingRooms.insert(conversationId);
		const bool queued = QMetaObject::invokeMethod(FDatabaseWorker, "loadHistory", Qt::QueuedConnection,
			Q_ARG(QString, FDatabaseProfileDirectory),
			Q_ARG(QString, FDatabaseServerUrl), Q_ARG(QString, FDatabaseUserId),
			Q_ARG(QString, conversationId));
		if (!queued) {
			const_cast<Matrix *>(this)->finishHistoryLoad(conversationId);
		}
	}
	return result;
}

bool Matrix::supportsOlderHistory(const ConversationId &conversationId) const
{
	return !conversationId.isEmpty() && FDatabaseWorker &&
		!FDatabaseProfileDirectory.isEmpty() && !FDatabaseServerUrl.isEmpty() &&
		!FDatabaseUserId.isEmpty();
}

bool Matrix::requestOlderHistoryPage(const ConversationId &conversationId,
	const BasicMessage &beforeMessage, int limit, QObject *callbackContext,
	ProtocolHistoryPageCallback callback)
{
	if (!supportsOlderHistory(conversationId) || beforeMessage.conversationId() != conversationId ||
		!beforeMessage.timestamp().isValid() || beforeMessage.messageId().isEmpty() ||
		limit <= 0 || !callbackContext || !callback ||
		FPendingHistoryPageRequests.contains(conversationId))
		return false;

	PendingHistoryPageRequest pending;
	pending.callbackContext = callbackContext;
	pending.callback = callback;
	FPendingHistoryPageRequests.insert(conversationId, pending);
	MatrixDatabaseWorker *worker = FDatabaseWorker;
	const QString profileDirectory = FDatabaseProfileDirectory;
	const QString serverUrl = FDatabaseServerUrl;
	const QString userId = FDatabaseUserId;
	const QString roomId = conversationId;
	const int pageLimit = qMin(limit, 30);
	const qint64 beforeOriginTs = beforeMessage.timestamp().toMSecsSinceEpoch();
	const QString beforeEventId = beforeMessage.messageId();
	const bool queued = QMetaObject::invokeMethod(worker,
		[worker, profileDirectory, serverUrl, userId, roomId, pageLimit,
			beforeOriginTs, beforeEventId]() {
			worker->loadHistoryPage(profileDirectory, serverUrl, userId, roomId,
				pageLimit, beforeOriginTs, beforeEventId);
		}, Qt::QueuedConnection);
	if (!queued)
		FPendingHistoryPageRequests.remove(conversationId);
	return queued;
}

void Matrix::setActiveConversation(const QString &conversationId) const
{
	FActiveConversationId = conversationId;
	if (!FMatrixNetwork)
		return;
	QMetaObject::invokeMethod(FMatrixNetwork, "setActiveRoom", Qt::QueuedConnection,
		Q_ARG(QString, conversationId));
	if (!FNetworkLoggedIn || conversationId.isEmpty())
		return;

	const ProtocolRoom currentRoom = room(conversationId);
	if (currentRoom.id.isEmpty())
		return;
	bool missingDisplayName = currentRoom.name.trimmed().isEmpty() ||
		currentRoom.name == currentRoom.id;
	bool missingAvatar = currentRoom.avatarUrl.trimmed().isEmpty();
	if (currentRoom.isDirect) {
		for (const ProtocolRosterEntry &member : currentRoom.members) {
			if (member.id.isEmpty() || member.id == FNetworkUserId)
				continue;
			missingDisplayName = missingDisplayName || member.name.trimmed().isEmpty() ||
				member.name == member.id;
			missingAvatar = missingAvatar || member.avatarUrl.trimmed().isEmpty();
		}
	}
	const QString scopeKey = accountId() + QChar('\n') + conversationId;
	if ((missingDisplayName || missingAvatar) &&
		FMetadataRefreshPolicy.shouldCheck(scopeKey, true, FMetadataRefreshClock.elapsed()))
		QMetaObject::invokeMethod(FMatrixNetwork, "refreshRoomMetadata",
			Qt::QueuedConnection, Q_ARG(QString, conversationId));
}

void Matrix::loadConversationAvatars(const QString &conversationId) const
{
	const ProtocolRoom currentRoom = room(conversationId);
	enqueueAvatarLoad(currentRoom.avatarKey, currentRoom.avatarUrl);
	if (!FAvatarLoadScheduled && !FAvatarLoadQueue.isEmpty()) {
		FAvatarLoadScheduled = true;
		QTimer::singleShot(0, const_cast<Matrix *>(this), &Matrix::loadNextAvatar);
	}
}

void Matrix::loadUserAvatar(const QString &conversationId, const QString &userId) const
{
	const ProtocolRoom currentRoom = room(conversationId);
	for (const ProtocolRosterEntry &member : currentRoom.members)
		if (member.id == userId) {
			enqueueAvatarLoad(userAvatarKey(conversationId, userId), member.avatarUrl);
			break;
		}
	if (!FAvatarLoadScheduled && !FAvatarLoadQueue.isEmpty()) {
		FAvatarLoadScheduled = true;
		QTimer::singleShot(0, const_cast<Matrix *>(this), &Matrix::loadNextAvatar);
	}
}

void Matrix::loadRoomAvatar(const QString &roomId) const
{
	const ProtocolRoom currentRoom = room(roomId);
	const QString key = currentRoom.avatarKey.isEmpty()
		? accountId() + QStringLiteral("\nroom\n") + currentRoom.id
		: currentRoom.avatarKey;
	enqueueAvatarLoad(key, currentRoom.avatarUrl);
	if (!FAvatarLoadScheduled && !FAvatarLoadQueue.isEmpty()) {
		FAvatarLoadScheduled = true;
		QTimer::singleShot(0, const_cast<Matrix *>(this), &Matrix::loadNextAvatar);
	}
}

void Matrix::loadConversationMedia(const BasicMessage &message) const
{
	if (!FMatrixNetwork || message.conversationId().isEmpty() || message.messageId().isEmpty())
		return;
	MatrixTextEvent event;
	event.eventId = message.messageId();
	event.roomId = message.conversationId();
	event.userId = message.sender();
	event.content = message.body();
	event.timestamp = QString::number(message.timestamp().toMSecsSinceEpoch());
	event.eventType = QStringLiteral("m.room.message");
	event.messageType = message.metadata().value(QStringLiteral("msgtype")).toString();
	event.metadata = message.metadata();
	MatrixNetwork *network = FMatrixNetwork;
	QMetaObject::invokeMethod(network, [network, event]() { network->requestImage(event); },
		Qt::QueuedConnection);
}

void Matrix::enqueueAvatarLoad(const QString &key, const QString &url) const
{
	if (key.isEmpty() || url.isEmpty())
		return;
	const QPair<QString, QString> item(key, url);
	const QString requestKey = key + QChar('\n') + url;
	if (FAvatarLoadSeen.contains(requestKey))
		return;
	FAvatarLoadSeen.insert(requestKey);
	if (FAvatarLoadQueue.contains(item))
		return;
	FAvatarLoadQueue.append(item);
}

void Matrix::loadNextAvatar()
{
	if (FAvatarLoadQueue.isEmpty()) {
		FAvatarLoadScheduled = false;
		return;
	}
	const auto item = FAvatarLoadQueue.takeFirst();
	if (FMatrixNetwork)
		QMetaObject::invokeMethod(FMatrixNetwork, "requestAvatar", Qt::QueuedConnection,
			Q_ARG(QString, item.first), Q_ARG(QString, item.second));
	if (!FAvatarLoadQueue.isEmpty())
		QTimer::singleShot(1000, this, &Matrix::loadNextAvatar);
	else
		FAvatarLoadScheduled = false;
}

QString Matrix::conversationDisplayName(const QString &conversationId) const
{
	const ProtocolRoom currentRoom = room(conversationId);
	return currentRoom.name.isEmpty() ? conversationId : currentRoom.name;
}

static QString matrixAvatarCachePath(const QString &profileDirectory, const QString &mxcUrl)
{
	if (mxcUrl.isEmpty())
		return QString();
	const QString directory = profileDirectory.isEmpty()
		? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/matrix-avatars")
		: profileDirectory + QStringLiteral("/avatars");
	return directory + QLatin1Char('/') +
		QString::fromLatin1(QCryptographicHash::hash(mxcUrl.toUtf8(), QCryptographicHash::Sha256).toHex()) +
		QStringLiteral(".bin");
}

QString Matrix::conversationAvatarPath(const QString &conversationId) const
{
	return matrixAvatarCachePath(FDatabaseProfileDirectory, room(conversationId).avatarUrl);
}

QString Matrix::userAvatarPath(const QString &conversationId, const QString &userId) const
{
	const ProtocolRoom currentRoom = room(conversationId);
	for (const ProtocolRosterEntry &member : currentRoom.members)
		if (member.id == userId)
			return matrixAvatarCachePath(FDatabaseProfileDirectory, member.avatarUrl);
	return QString();
}

QString Matrix::avatarUrlForUser(const QString &userId) const
{
	for (const ProtocolRoom &room : FProtocolRooms)
		for (const ProtocolRosterEntry &member : room.members)
			if (member.id == userId && !member.avatarUrl.isEmpty())
				return member.avatarUrl;
	return QString();
}

QString Matrix::userAvatarKey(const QString &conversationId, const QString &userId) const
{
	Q_UNUSED(conversationId);
	return MatrixAvatarKeyPolicy::userAvatarKey(accountId(), userId,
		avatarUrlForUser(userId));
}

QString Matrix::accountAvatarKey() const
{
	if (FNetworkUserId.isEmpty())
		return QString();
	const QString avatarUrl = avatarUrlForUser(FNetworkUserId);
	return avatarUrl.isEmpty() ? QString()
		: MatrixAvatarKeyPolicy::userAvatarKey(accountId(), FNetworkUserId, avatarUrl);
}

void Matrix::loadAccountAvatar() const
{
	if (FNetworkUserId.isEmpty())
		return;
	const QString avatarUrl = avatarUrlForUser(FNetworkUserId);
	if (avatarUrl.isEmpty())
		return;
	enqueueAvatarLoad(accountAvatarKey(), avatarUrl);
	if (!FAvatarLoadScheduled && !FAvatarLoadQueue.isEmpty()) {
		FAvatarLoadScheduled = true;
		QTimer::singleShot(0, const_cast<Matrix *>(this), &Matrix::loadNextAvatar);
	}
}

bool Matrix::markConversationRead(const QString &conversationId, const QString &eventId)
{
	if (!FMatrixNetwork || !FNetworkLoggedIn)
		return false;
	QMetaObject::invokeMethod(FMatrixNetwork, "markRoomRead", Qt::QueuedConnection,
		Q_ARG(QString, conversationId), Q_ARG(QString, eventId));
	for (int i = FNotifications.size() - 1; i >= 0; --i) {
		if (FNotifications.at(i).conversationId == conversationId)
			FNotificationEventIds.remove(FNotifications.at(i).id);
		if (FNotifications.at(i).conversationId == conversationId)
			FNotifications.removeAt(i);
	}
	emit protocolNotificationsChanged();
	return true;
}

QString Matrix::latestConversationEventId(const QString &conversationId) const
{
	if (!FMatrixNetwork)
		return QString();
	Q_UNUSED(conversationId);
	return FLatestConversationEventIds.value(conversationId);
}

bool Matrix::conversationIdForAddress(const Jid &address, QString &conversationId) const
{
	const QString local = address.node();
	if (!local.startsWith(QStringLiteral("room-")))
		return false;
	const QByteArray roomId = QByteArray::fromHex(local.mid(5).toLatin1());
	if (roomId.isEmpty())
		return false;
	conversationId = QString::fromUtf8(roomId);
	return true;
}

Jid Matrix::addressForConversation(const QString &conversationId) const
{
	const QString local = QStringLiteral("room-") + QString::fromLatin1(conversationId.toUtf8().toHex());
	return Jid::fromUserInput(local + QStringLiteral("@protocol.local"));
}

bool Matrix::conversationOnline(const Jid &address) const
{
	QString conversationId;
	if (!conversationIdForAddress(address, conversationId))
		return false;
	for (const ProtocolRoom &room : FProtocolRooms)
		if (room.id == conversationId)
			return room.isJoined;
	return false;
}

QList<ProtocolNotification> Matrix::notifications() const
{
	return FNotifications;
}

void Matrix::appendNotification(const ProtocolNotification &notification)
{
	ProtocolNotification stored = notification;
	if (stored.id.isEmpty())
		return;
	if (!stored.conversationId.isEmpty() &&
		room(stored.conversationId).roomType == QStringLiteral("m.space"))
		return;
	if (stored.accountId.isEmpty())
		stored.accountId = accountId();
	if (stored.streamId.isEmpty())
		stored.streamId = streamId();
	for (const ProtocolNotification &existing : FNotifications)
		if (existing.id == stored.id)
			return;
	FNotifications.append(stored);
	FNotificationEventIds.insert(stored.id);
	emit protocolNotificationsChanged();
}

void Matrix::removeNotification(const QString &id)
{
	for (int i = FNotifications.size() - 1; i >= 0; --i) {
		if (FNotifications.at(i).id == id) {
			FNotifications.removeAt(i);
			break;
		}
	}
	FNotificationEventIds.remove(id);
	// The event id is removed from the in-memory view; room-level cleanup is
	// performed when a conversation is explicitly marked read.
	emit protocolNotificationsChanged();
}

bool Matrix::isDisplayedLocalEchoDuplicate(const BasicMessage &message) const
{
	if (message.conversationId().isEmpty())
		return false;
	const QString roomPrefix = message.conversationId() + QChar('\n');
	const QVariantMap metadata = message.metadata();
	for (const QString &key : {QStringLiteral("replaces_txn_id"),
		QStringLiteral("txn_id")}) {
		const QString transactionId = metadata.value(key).toString();
		if (!transactionId.isEmpty() &&
			FDisplayedLocalEchoTransactions.contains(roomPrefix + transactionId))
			return true;
	}
	return !message.messageId().isEmpty() &&
		FDisplayedLocalEchoTransactions.contains(roomPrefix + message.messageId());
}

void Matrix::onNetworkMessageReceived(const BasicMessage &message)
{
	BasicMessage normalizedMessage = message;
	QVariantMap metadata = normalizedMessage.metadata();
	const bool senderIsSelf = message.direction() == BasicMessage::Outgoing ||
		(!FNetworkUserId.isEmpty() ? FNetworkUserId == message.sender() :
			(metadata.value(QStringLiteral("sender_is_self")).toBool() ||
			 MatrixSessionPolicy::matchesLoginUserId(FDatabaseUserId, message.sender())));
	metadata.insert(QStringLiteral("sender_is_self"), senderIsSelf);
	normalizedMessage.setMetadata(metadata);
	if (!normalizedMessage.conversationId().isEmpty() && !normalizedMessage.messageId().isEmpty())
		FLatestConversationEventIds.insert(normalizedMessage.conversationId(), normalizedMessage.messageId());
	if (FNotificationsReady && FMatrixNetwork &&
		normalizedMessage.metadata().value(QStringLiteral("room_type")).toString() != QStringLiteral("m.space") &&
		room(normalizedMessage.conversationId()).roomType != QStringLiteral("m.space") &&
		normalizedMessage.metadata().value(QStringLiteral("event_type")).toString() != QStringLiteral("m.reaction") &&
		!normalizedMessage.metadata().value(QStringLiteral("historical")).toBool() &&
		normalizedMessage.direction() == BasicMessage::Incoming &&
		normalizedMessage.sender() != FNetworkUserId &&
		normalizedMessage.conversationId() != FActiveConversationId &&
		!normalizedMessage.messageId().isEmpty() &&
		!FNotificationEventIds.contains(normalizedMessage.messageId())) {
		ProtocolNotification notification;
		notification.id = normalizedMessage.messageId();
		notification.accountId = accountId();
		notification.conversationId = normalizedMessage.conversationId();
		notification.title = normalizedMessage.sender();
		notification.body = normalizedMessage.body();
		notification.protocol = protocol();
		notification.timestamp = normalizedMessage.timestamp();
		notification.kind = normalizedMessage.metadata().value(QStringLiteral("highlight")).toBool()
			? ProtocolNotification::Mention : ProtocolNotification::Message;
		appendNotification(notification);
	}
	queueOrEmitHistoryMessage(normalizedMessage);
}

void Matrix::onAvatarImageReceived(const QString &key, const QImage &image)
{
	if (!FAvatars || image.isNull())
		return;
	FAvatars->setCustomImageByKey(key, image);
	emit protocolAvatarUpdated(key);
}

void Matrix::onDisplayNameReceived(const QString &userId, const QString &displayName)
{
	bool changed = false;
	for (ProtocolRoom &room : FProtocolRooms)
		for (ProtocolRosterEntry &member : room.members)
			if (member.id == userId && member.name != displayName) {
				member.name = displayName;
				changed = true;
			}
	if (changed) {
		emit protocolRosterChanged();
	}
}

void Matrix::onRoomNameReceived(const QString &roomId, const QString &roomName)
{
	for (ProtocolRoom &room : FProtocolRooms) {
		if (room.id != roomId)
			continue;
		room.name = roomName.isEmpty() ? roomId : roomName;
		emit protocolRosterChanged();
		return;
	}
}

void Matrix::onNetworkTypingChanged(const ProtocolTypingUpdate &update)
{
	ProtocolTypingUpdate translated = update;
	translated.accountId = FStreamId;
	emit protocolTypingChanged(translated);
}

void Matrix::onNetworkReceiptReceived(const MatrixReceipt &receipt)
{
	if (receipt.receiptType != QStringLiteral("m.read") || receipt.roomId.isEmpty() ||
		receipt.eventId.isEmpty() || receipt.userId.isEmpty() || receipt.userId == FNetworkUserId)
		return;

	QString readerDisplayName = receipt.userId;
	const ProtocolRoom currentRoom = room(receipt.roomId);
	for (const ProtocolRosterEntry &member : currentRoom.members)
		if (member.id == receipt.userId) {
			if (!member.name.trimmed().isEmpty())
				readerDisplayName = member.name;
			break;
		}
	emit protocolMessageReadReceiptReceived(receipt.roomId, receipt.eventId,
		receipt.userId, readerDisplayName);
}

void Matrix::onNetworkNotificationEvent(const MatrixNotificationEvent &event)
{
	if (event.roomType == QStringLiteral("m.space") || event.historical ||
		event.eventId.isEmpty() || event.sender == FNetworkUserId ||
		FNotificationEventIds.contains(event.eventId))
		return;
	ProtocolNotification notification;
	notification.id = event.eventId;
	notification.accountId = accountId();
	notification.conversationId = event.roomId;
	notification.title = event.sender;
	notification.protocol = protocol();
	notification.timestamp = MatrixTimestamps::fromUnixMilliseconds(event.timestamp);
	const QString membership = event.content.value(QStringLiteral("membership")).toString();
	if (event.type == QStringLiteral("m.room.member") &&
		event.content.value(QStringLiteral("membership")).toString() == QStringLiteral("invite")) {
		notification.kind = ProtocolNotification::Invite;
		notification.body = tr("You were invited to a Matrix room");
	} else if (event.type == QStringLiteral("m.reaction")) {
		notification.kind = ProtocolNotification::Reaction;
		notification.body = event.content.value(QStringLiteral("m.relates_to")).toObject()
		.value(QStringLiteral("key")).toString();
	} else if (event.type == QStringLiteral("m.call.invite") ||
		event.type == QStringLiteral("m.call.member")) {
		notification.kind = ProtocolNotification::Call;
		notification.body = tr("Incoming Matrix call");
	} else if (event.type == QStringLiteral("m.room.member")) {
		notification.kind = ProtocolNotification::Membership;
		notification.body = membership.isEmpty() ? tr("Room membership changed")
		: tr("Membership changed to %1").arg(membership);
	} else if (event.type == QStringLiteral("m.key.verification.request") ||
		event.type == QStringLiteral("m.key.verification.done") ||
		event.type == QStringLiteral("m.key.verification.mac")) {
		notification.kind = ProtocolNotification::Verification;
		notification.body = tr("Matrix verification event");
	} else if (event.type == QStringLiteral("m.room.redaction")) {
		notification.kind = ProtocolNotification::Info;
		notification.body = tr("A Matrix event was redacted");
	} else {
		return;
	}
	appendNotification(notification);
}

void Matrix::onLoginError(const QString &AError, quint64 ASessionGeneration)
{
	if (ASessionGeneration != FLoginGeneration || !FLoginRequested)
		return;
	FLoginRequested = false;
	FLoginAccountId.clear();
	FLoginUserId.clear();
	FPendingShow = IPresence::Offline;
	FPendingStatus.clear();
	FNetworkLoggedIn = false;
	FNetworkInitialSyncComplete = false;
	FNetworkUserId.clear();
	FShow = 0;
	FStatus.clear();
	if (!FStreamId.isEmpty())
		emit protocolPresenceChanged(FStreamId, IPresence::Offline, QStringLiteral("Offline"));
	qWarning() << "Matrix login error:" << AError;
}

void Matrix::onSyncError(const QString &AError)
{
	qWarning() << "Matrix sync error:" << AError;
}

void Matrix::onAccountAvatarUpdateFinished(const QString &userId, bool success,
	const QString &avatarUrl, const QString &error)
{
	if (userId != FNetworkUserId)
		return;
	if (success) {
		bool changed = false;
		for (ProtocolRoom &room : FProtocolRooms)
			for (ProtocolRosterEntry &member : room.members)
				if (member.id == userId && member.avatarUrl != avatarUrl) {
					member.avatarUrl = avatarUrl;
					changed = true;
				}
		if (changed)
			emit protocolRosterChanged();
		return;
	}
	QWidget *parent = FRostersViewPlugin && FRostersViewPlugin->rostersView()
		? FRostersViewPlugin->rostersView()->instance() : nullptr;
	QMessageBox::warning(parent, tr("Matrix avatar update"), error.isEmpty()
		? tr("The Matrix avatar could not be updated.") : error);
}

void Matrix::onAccountDisplayNameUpdateFinished(const QString &userId, bool success,
	const QString &displayName, const QString &error)
{
	if (userId != FNetworkUserId)
		return;
	if (success) {
		onDisplayNameReceived(userId, displayName);
		return;
	}
	QWidget *parent = FRostersViewPlugin && FRostersViewPlugin->rostersView()
		? FRostersViewPlugin->rostersView()->instance() : nullptr;
	QMessageBox::warning(parent, tr("Matrix profile update"), error.isEmpty()
		? tr("The Matrix display name could not be updated.") : error);
}

void Matrix::onSyncReceived(const QList<MatrixTextEvent> &events)
{
	FNotificationsReady = true;
	enqueueProtocolEventBatch(events);
}

void Matrix::onMessageHistoryChanged(const QString &roomId,
                                     const QList<MatrixTextEvent> &events)
{
	if (roomId.isEmpty())
		return;
	enqueueProtocolEventBatch(events, roomId);
}

void Matrix::enqueueProtocolEventBatch(const QList<MatrixTextEvent> &events,
	const QString &historyRoomId)
{
	if (events.isEmpty())
		return;
	FPendingProtocolEventBatches.append({events, historyRoomId});
	if (FProtocolEventBatchScheduled)
		return;
	FProtocolEventBatchScheduled = true;
	QTimer::singleShot(0, this, &Matrix::processNextProtocolEventBatch);
}

void Matrix::processNextProtocolEventBatch()
{
	constexpr qsizetype maxEventsPerTurn = 25;
	constexpr qint64 maxTurnDurationMs = 5;
	QElapsedTimer timer;
	timer.start();
	qsizetype processed = 0;

	while (!FPendingProtocolEventBatches.isEmpty() && processed < maxEventsPerTurn &&
		timer.elapsed() < maxTurnDurationMs) {
		PendingProtocolEventBatch &pending = FPendingProtocolEventBatches.first();
		if (FPendingProtocolEventIndex >= pending.events.size()) {
			FPendingProtocolEventBatches.removeFirst();
			FPendingProtocolEventIndex = 0;
			continue;
		}

		const MatrixTextEvent event = pending.events.at(FPendingProtocolEventIndex++);
		const bool fromHistorySnapshot = !pending.historyRoomId.isEmpty();
		++processed;
		if (fromHistorySnapshot) {
			const bool encrypted = event.metadata.value(QStringLiteral("outer_event_type")).toString() ==
				QStringLiteral("m.room.encrypted");
			if (event.roomId != pending.historyRoomId || (encrypted &&
				event.metadata.value(QStringLiteral("decryption_status")).toString() !=
					QStringLiteral("decrypted")))
				continue;
		}

		if (!fromHistorySnapshot && event.activeImageFetchPending)
			continue;

		const BasicMessage message = event.toBasicMessage();
		if (fromHistorySnapshot)
			queueOrEmitHistoryMessage(message);
		else
			onNetworkMessageReceived(message);
	}

	while (!FPendingProtocolEventBatches.isEmpty() &&
		FPendingProtocolEventIndex >= FPendingProtocolEventBatches.first().events.size()) {
		FPendingProtocolEventBatches.removeFirst();
		FPendingProtocolEventIndex = 0;
	}

	if (FPendingProtocolEventBatches.isEmpty()) {
		FProtocolEventBatchScheduled = false;
		return;
	}
	QTimer::singleShot(1, this, &Matrix::processNextProtocolEventBatch);
}

void Matrix::onCachedHistoryLoadFailed(const QString &roomId, const QString &error)
{
	qWarning() << "Matrix cached history load failed for" << roomId << ":" << error;
	FHistoryRequests.remove(roomId);
	finishHistoryLoad(roomId);
}

void Matrix::queueOrEmitHistoryMessage(const BasicMessage &message)
{
	if (isDisplayedLocalEchoDuplicate(message))
		return;
	const QString roomId = message.conversationId();
	if (!roomId.isEmpty() && FHistoryLoadingRooms.contains(roomId)) {
		FPendingHistoryMessages[roomId].append(message);
		return;
	}
	emit protocolMessageReceived(message);
}

void Matrix::finishHistoryLoad(const QString &roomId)
{
	if (FCachedHistoryRemainingByRoom.value(roomId) > 0)
		return;
	const QList<BasicMessage> pendingMessages = FPendingHistoryMessages.take(roomId);
	const QList<BasicMessage> orderedPendingMessages =
		mergeHistoryMessagesChronologically(QList<BasicMessage>(), pendingMessages);
	if (!orderedPendingMessages.isEmpty()) {
		FCachedHistoryQueue.append(orderedPendingMessages);
		FCachedHistoryRemainingByRoom[roomId] += orderedPendingMessages.size();
		if (!FCachedHistoryBatchScheduled) {
			FCachedHistoryBatchScheduled = true;
			QTimer::singleShot(0, this, &Matrix::emitCachedHistoryBatch);
		}
		return;
	}
	FCachedHistoryRemainingByRoom.remove(roomId);
	FHistoryLoadingRooms.remove(roomId);
	FHistoryRequests.remove(roomId);
	emit protocolHistoryLoaded(roomId);
}

QList<BasicMessage> Matrix::mergeHistoryMessagesChronologically(
	const QList<BasicMessage> &cached, const QList<BasicMessage> &pending) const
{
	QHash<QString, int> messageIndexes;
	QList<BasicMessage> messages;
	auto addMessage = [&messages, &messageIndexes](const BasicMessage &message) {
		const QString messageId = message.messageId();
		if (messageId.isEmpty()) {
			messages.append(message);
			return;
		}
		const auto existing = messageIndexes.constFind(messageId);
		if (existing == messageIndexes.constEnd()) {
			messageIndexes.insert(messageId, messages.size());
			messages.append(message);
		} else {
			messages[existing.value()] = message;
		}
	};
	for (const BasicMessage &message : cached)
		addMessage(message);
	for (const BasicMessage &message : pending)
		addMessage(message);
	std::stable_sort(messages.begin(), messages.end(), [](const BasicMessage &left,
		const BasicMessage &right) {
		const QDateTime leftTime = left.timestamp();
		const QDateTime rightTime = right.timestamp();
		if (leftTime.isValid() != rightTime.isValid())
			return leftTime.isValid();
		if (!leftTime.isValid())
			return false;
		return leftTime.toMSecsSinceEpoch() < rightTime.toMSecsSinceEpoch();
	});
	return messages;
}

void Matrix::onRoomMetadataChanged(const ProtocolRoom &updatedRoom)
{
	for (ProtocolRoom &room : FProtocolRooms) {
		if (room.id != updatedRoom.id)
			continue;
		const QString previousAvatarUrl = room.avatarUrl;
		ProtocolRoom refreshed = updatedRoom;
		if (refreshed.roomType.isEmpty())
			refreshed.roomType = room.roomType;
		refreshed.avatarKey = accountId() + QStringLiteral("\nroom\n") + refreshed.id;
		room = refreshed;
		if (room.id == FActiveConversationId && !room.avatarUrl.isEmpty() &&
			room.avatarUrl != previousAvatarUrl)
			loadRoomAvatar(room.id);
		emit protocolRosterChanged();
		return;
	}
}

void Matrix::onRosterChanged(const QList<ProtocolRoom> &rooms)
{
	if (rooms.isEmpty() && !FProtocolRooms.isEmpty())
		return;
	const QList<ProtocolRoom> previousRooms = FProtocolRooms;
	FProtocolRooms = rooms;
	FProtocolEntries.clear();
	for (ProtocolRoom &room : FProtocolRooms) {
		if (room.roomType.isEmpty())
			for (const ProtocolRoom &cachedRoom : previousRooms)
				if (cachedRoom.id == room.id && !cachedRoom.roomType.isEmpty()) {
					room.roomType = cachedRoom.roomType;
					break;
				}
		room.avatarKey = accountId() + QStringLiteral("\nroom\n") + room.id;
	}
	if (FMatrixNetwork)
	{
		for (ProtocolRoom &room : FProtocolRooms)
		{
			if (!room.isDirect || !room.avatarUrl.isEmpty()) continue;
			for (const ProtocolRosterEntry &member : room.members)
				if (member.id != FNetworkUserId && !member.avatarUrl.isEmpty())
				{ room.avatarUrl = member.avatarUrl; break; }
		}
	}
	// Matrix conversations are rooms. A direct room remains one room-list
	// item; its direct-chat presentation is derived from ProtocolRoom::isDirect.
	// Do not expose the other member as a second roster item for the same room.

	emit protocolRosterChanged();
}

// Compile/link check for the official libolm API. Crypto state is added in
// the Matrix E2EE layer; this probe deliberately handles no user data.
#if HAVE_OLM
#include <olm/olm.h>

namespace OlmTest {
	[[maybe_unused]] void testOlmApi()
	{
		(void)olm_account_size();
		(void)olm_session_size();
	}
}
#endif
