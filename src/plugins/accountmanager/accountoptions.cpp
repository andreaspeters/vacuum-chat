#include "accountoptions.h"

#include <QTimer>
#include <QMessageBox>
#include <QTextDocument>
#include <QFormLayout>
#include <QVBoxLayout>
#include <interfaces/iemoticons.h>

AccountOptions::AccountOptions(IAccountManager *AManager, IPluginManager *APluginManager, const QUuid &AAccountId, QWidget *AParent) : QWidget(AParent)
{
	ui.setupUi(this);
	setMinimumWidth(590);
	FAccountType = new QComboBox(this);
	FAccountType->addItem(tr("Jabber / XMPP"), QStringLiteral("jabber"));
	FAccountType->addItem(tr("Matrix"), QStringLiteral("matrix"));
	FAccountType->addItem(tr("MeshCore"), QStringLiteral("meshcore"));
	if (QVBoxLayout *root = qobject_cast<QVBoxLayout *>(layout()))
		root->insertWidget(0, FAccountType);

	// Setup Matrix fields
	FMatrixFields = new QWidget(this);
	QFormLayout *matrixLayout = new QFormLayout(FMatrixFields);
	FMatrixInstance = new QLineEdit(FMatrixFields);
	FMatrixUsername = new QLineEdit(FMatrixFields);
	FMatrixPassword = new QLineEdit(FMatrixFields);
	FMatrixDeviceId = new QLineEdit(FMatrixFields);
	FMatrixDeviceId->setReadOnly(true);
	FMatrixEmojiPack = new QComboBox(FMatrixFields);
	FMatrixEmojiPack->addItem(tr("Unicode Emoji (:lol: inserts 😂)"), QStringLiteral("unicode"));
	FMatrixEmojiPack->addItem(tr("Text / Shortcodes"), QStringLiteral("text"));

	FMatrixSsssButton = new QPushButton(tr("Recover Matrix cross-signing (SSSS)"), FMatrixFields);
	FMatrixRoomKeyImportButton = new QPushButton(tr("Import Matrix room keys"), FMatrixFields);
	FMatrixNewDeviceButton = new QPushButton(tr("Register as new Matrix device"), FMatrixFields);
	FMatrixTargetUser = new QLineEdit(FMatrixFields);
	FMatrixTargetDevice = new QLineEdit(FMatrixFields);
	FMatrixVerifyButton = new QPushButton(tr("Verify Matrix device"), FMatrixFields);
	FMatrixPassword->setEchoMode(QLineEdit::Password);
	FMatrixInstance->setPlaceholderText(QStringLiteral("https://matrix.example.org"));
	matrixLayout->addRow(tr("Matrix instance:"), FMatrixInstance);
	matrixLayout->addRow(tr("Username:"), FMatrixUsername);
	matrixLayout->addRow(tr("Password:"), FMatrixPassword);
	matrixLayout->addRow(tr("Current Vacuum device ID:"), FMatrixDeviceId);
	matrixLayout->addRow(tr("Emoji type:"), FMatrixEmojiPack);

	matrixLayout->addRow(FMatrixSsssButton);
	matrixLayout->addRow(FMatrixRoomKeyImportButton);
	matrixLayout->addRow(FMatrixNewDeviceButton);
	matrixLayout->addRow(tr("Target user ID:"), FMatrixTargetUser);
	matrixLayout->addRow(tr("Target device ID:"), FMatrixTargetDevice);
	matrixLayout->addRow(FMatrixVerifyButton);

	connect(FMatrixSsssButton, &QPushButton::clicked, this, [this]() {
		emit matrixSsssRecoveryRequested(FAccountId);
	});
	connect(FMatrixRoomKeyImportButton, &QPushButton::clicked, this, [this]() {
		emit matrixRoomKeyImportRequested(FAccountId);
	});
	connect(FMatrixNewDeviceButton, &QPushButton::clicked, this, [this]() {
		FMatrixDeviceId->clear();
		QMessageBox::information(this, tr("New Matrix device"),
			tr("Save the account settings and log in again. Matrix will assign a new device ID."));
		emit modified();
	});
	connect(FMatrixVerifyButton, &QPushButton::clicked, this, [this]() {
		const QString userId = FMatrixTargetUser->text().trimmed();
		const QString deviceId = FMatrixTargetDevice->text().trimmed();
		if (userId.isEmpty() || deviceId.isEmpty()) {
			QMessageBox::warning(this, tr("Matrix verification"),
				tr("Enter both the target user ID and target device ID."));
			return;
		}
		qWarning() << "[Matrix-E2EE] account form verification requested:" << userId << deviceId;
		emit matrixVerificationRequested(FAccountId, userId, deviceId);
	});
	if (QVBoxLayout *root = qobject_cast<QVBoxLayout *>(layout()))
		root->insertWidget(1, FMatrixFields);
	FMatrixFields->hide();

	// Setup MeshCore fields
	FMeshCoreFields = new QWidget(this);
	QFormLayout *meshcoreLayout = new QFormLayout(FMeshCoreFields);
	FMeshCoreTransport = new QComboBox(FMeshCoreFields);
	FMeshCoreTransport->addItem(tr("BLE"), QStringLiteral("ble"));
	FMeshCoreTransport->addItem(tr("USB"), QStringLiteral("usb"));
	FMeshCoreMacAddress = new QLineEdit(FMeshCoreFields);
	FMeshCorePort = new QLineEdit(FMeshCoreFields);

	// Set default values
	FMeshCoreMacAddress->setText(QStringLiteral("10:BD:A3:5A:6B:E9"));
	FMeshCorePort->setText(QStringLiteral("/dev/ttyACM0"));

	meshcoreLayout->addRow(tr("Transport:"), FMeshCoreTransport);
	meshcoreLayout->addRow(tr("BLE MAC Address:"), FMeshCoreMacAddress);
	meshcoreLayout->addRow(tr("USB Port:"), FMeshCorePort);

	if (QVBoxLayout *root = qobject_cast<QVBoxLayout *>(layout()))
		root->insertWidget(2, FMeshCoreFields);
	FMeshCoreFields->hide();

	const auto updateAccountTypeUi = [this](int AIndex) {
		const bool matrix = FAccountType->itemData(AIndex).toString() == QStringLiteral("matrix");
		const bool meshcore = FAccountType->itemData(AIndex).toString() == QStringLiteral("meshcore");
		ui.grbAccount->setVisible(!matrix && !meshcore);
		FMatrixFields->setVisible(matrix);
		FMatrixFields->setEnabled(matrix);
		FMeshCoreFields->setVisible(meshcore);
		FMeshCoreFields->setEnabled(meshcore);
	};
	connect(FAccountType, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		[this, updateAccountTypeUi](int AIndex) {
			updateAccountTypeUi(AIndex);
			if (FAccountType->itemData(AIndex).toString() == QStringLiteral("meshcore") &&
				ui.lneName->text().trimmed() == tr("New Account"))
				ui.lneName->setText(QStringLiteral("@Meshcore"));
			emit modified();
		});
	updateAccountTypeUi(FAccountType->currentIndex());
	FManager = AManager;
	FPluginManager = APluginManager;
	if (FPluginManager) {
		const QList<IPlugin *> emoticonPlugins = FPluginManager->pluginInterface("IEmoticons");
		if (!emoticonPlugins.isEmpty()) {
			IEmoticons *emoticons = qobject_cast<IEmoticons *>(emoticonPlugins.first()->instance());
			if (emoticons) {
				for (const QString &iconset : emoticons->availableIconsets()) {
					if (iconset != QStringLiteral("unicode"))
						FMatrixEmojiPack->addItem(iconset, iconset);
				}
			}
		}
	}

	FAccountId = AAccountId;
	FAccount = FManager->accountById(AAccountId);

	if (FAccount == NULL)
	{
		ui.lneResource->setText(CLIENT_NAME);
		ui.lneName->setText(tr("New Account"));
		ui.lneName->selectAll();
		QTimer::singleShot(0,ui.lneName,SLOT(setFocus()));
	}

	connect(ui.lneName,SIGNAL(textChanged(const QString &)),SIGNAL(modified()));
	connect(ui.lneJabberId,SIGNAL(textChanged(const QString &)),SIGNAL(modified()));
	connect(ui.lneResource,SIGNAL(textChanged(const QString &)),SIGNAL(modified()));
	connect(ui.lnePassword,SIGNAL(textChanged(const QString &)),SIGNAL(modified()));
	connect(FMatrixInstance,&QLineEdit::textChanged,this,[this](const QString &){ emit modified(); });
	connect(FMatrixUsername,&QLineEdit::textChanged,this,[this](const QString &){ emit modified(); });
	connect(FMatrixPassword,&QLineEdit::textChanged,this,[this](const QString &){ emit modified(); });
	connect(FMatrixEmojiPack, QOverload<int>::of(&QComboBox::currentIndexChanged),
		this, [this](int){ emit modified(); });
	
	// Connect meshcore signals
	connect(FMeshCoreTransport, QOverload<int>::of(&QComboBox::currentIndexChanged),
		this, [this](int){ emit modified(); });
	connect(FMeshCoreMacAddress,&QLineEdit::textChanged,this,[this](const QString &){ emit modified(); });
	connect(FMeshCorePort,&QLineEdit::textChanged,this,[this](const QString &){ emit modified(); });

	reset();
}

AccountOptions::~AccountOptions()
{
	if (FAccount == NULL)
	{
		Options::node(OPV_ACCOUNT_ROOT).removeChilds("account",FAccountId.toString());
	}
}

void AccountOptions::apply()
{
	FAccount = FAccount==NULL ? FManager->appendAccount(FAccountId) : FAccount;
	if (FAccount)
	{
		const bool matrix = FAccountType->currentData().toString() == QStringLiteral("matrix");
		const bool meshcore = FAccountType->currentData().toString() == QStringLiteral("meshcore");
		QString name = ui.lneName->text().trimmed();
		if (name.isEmpty())
			name = matrix ? FMatrixUsername->text().trimmed() : (meshcore ? QStringLiteral("@Meshcore") : ui.lneJabberId->text().trimmed());
		if (name.isEmpty())
			name = tr("New Account");

		FAccount->setName(name);
		OptionsNode accountOptions = FAccount->optionsNode();
		accountOptions.setValue(matrix ? QStringLiteral("matrix") : (meshcore ? QStringLiteral("meshcore") : QStringLiteral("jabber")), "type");

		bool changedJid = false;
		if (matrix)
		{
			accountOptions.setValue(FMatrixInstance->text().trimmed(), "matrix.instance");
			accountOptions.setValue(FMatrixUsername->text().trimmed(), "matrix.username");
			accountOptions.setValue(FMatrixEmojiPack->currentData().toString(), "matrix.emoji-pack");
			accountOptions.setValue(FMatrixDeviceId->text().trimmed(), "matrix.device-id");
			FAccount->setPassword(FMatrixPassword->text());
		}
		else if (meshcore)
		{
			// Store meshcore settings under meshcore.* namespace
			accountOptions.setValue(FMeshCoreTransport->currentData().toString(), "meshcore.transport");
			accountOptions.setValue(FMeshCoreMacAddress->text().trimmed(), "meshcore.mac");
			accountOptions.setValue(FMeshCorePort->text().trimmed(), "meshcore.port");
			FAccount->setPassword(QString()); // Empty password for meshcore
		}
		else
		{
			Jid jabberId = Jid::fromUserInput(ui.lneJabberId->text());
			jabberId.setResource(ui.lneResource->text());
			changedJid = (FAccount->streamJid() != jabberId);
			FAccount->setStreamJid(jabberId);
			FAccount->setPassword(ui.lnePassword->text());
		}

		if (matrix && (FMatrixInstance->text().trimmed().isEmpty() || FMatrixUsername->text().trimmed().isEmpty()))
			QMessageBox::warning(this,tr("Invalid Matrix Account"),tr("Account '%1' requires a Matrix instance and username").arg(name));
		else if (!matrix && !meshcore && !FAccount->isValid())
			QMessageBox::warning(this,tr("Invalid Account"),tr("Account '%1' is not valid, change its Jabber ID").arg(name));
		else if (changedJid && FAccount->isActive() && FAccount->xmppStream()->isConnected())
			QMessageBox::information(NULL,tr("Delayed Apply"),tr("Some options of account '%1' will be applied after disconnect").arg(name));
	}
	emit childApply();
}

void AccountOptions::reset()
{
	if (FAccount)
	{
		const QString type = FAccount->optionsNode().value("type").toString();
		const int typeIndex = FAccountType->findData(type.isEmpty() ? QStringLiteral("jabber") : type);
		FAccountType->setCurrentIndex(typeIndex >= 0 ? typeIndex : 0);
		FMatrixInstance->setText(FAccount->optionsNode().value("matrix.instance").toString());
		FMatrixUsername->setText(FAccount->optionsNode().value("matrix.username").toString());
		FMatrixPassword->setText(FAccount->password());
		FMatrixDeviceId->setText(FAccount->optionsNode().value("matrix.device-id").toString());
		QString emojiPack = FAccount->optionsNode().value("matrix.emoji-pack").toString();
		if (emojiPack.isEmpty()) {
			const QString legacyFormat = FAccount->optionsNode().value("matrix.emoji-format").toString();
			emojiPack = legacyFormat == QStringLiteral("xmpp")
				? QStringLiteral("text") : QStringLiteral("unicode");
		}
		const int emojiIndex = FMatrixEmojiPack->findData(emojiPack);
		FMatrixEmojiPack->setCurrentIndex(emojiIndex >= 0 ? emojiIndex : 0);
		ui.lneName->setText(FAccount->name());
		ui.lneJabberId->setText(FAccount->streamJid().uBare());
		ui.lneResource->setText(FAccount->streamJid().resource());
		ui.lnePassword->setText(FAccount->password());

		// Load meshcore settings if needed
		const QString meshcoreTransport = FAccount->optionsNode().value("meshcore.transport").toString();
		FMeshCoreTransport->setCurrentIndex(FMeshCoreTransport->findData(
			meshcoreTransport.isEmpty() ? QStringLiteral("ble") : meshcoreTransport));
		const QString meshcoreMac = FAccount->optionsNode().value("meshcore.mac").toString();
		FMeshCoreMacAddress->setText(meshcoreMac.isEmpty()
			? QStringLiteral("10:BD:A3:5A:6B:E9") : meshcoreMac);
		const QString meshcorePort = FAccount->optionsNode().value("meshcore.port").toString();
		FMeshCorePort->setText(meshcorePort.isEmpty()
			? QStringLiteral("/dev/ttyACM0") : meshcorePort);
	}
	emit childReset();
}
