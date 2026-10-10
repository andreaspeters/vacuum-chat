#include "accountoptions.h"

#include <QTimer>
#include <QMessageBox>
#include <QTextDocument>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <interfaces/iemoticons.h>
#ifndef OS2
#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothDeviceInfo>
#endif

AccountOptions::AccountOptions(IAccountManager *AManager, IPluginManager *APluginManager, const QUuid &AAccountId, QWidget *AParent) : QWidget(AParent)
{
	ui.setupUi(this);
	setMaximumWidth(400);
	FAccountType = new QComboBox(this);
	FAccountType->addItem(tr("Jabber / XMPP"), QStringLiteral("jabber"));
	FAccountType->addItem(tr("Matrix"), QStringLiteral("matrix"));
	FAccountType->addItem(tr("MeshCore"), QStringLiteral("meshcore"));
	FAccountType->addItem(tr("AX.25 Chat"), QStringLiteral("ax25"));
	if (QVBoxLayout *root = qobject_cast<QVBoxLayout *>(layout()))
		root->insertWidget(0, FAccountType);

	// Setup Matrix fields
	FMatrixFields = new QWidget(this);
	QFormLayout *matrixLayout = new QFormLayout(FMatrixFields);
	matrixLayout->setRowWrapPolicy(QFormLayout::WrapLongRows);
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
	meshcoreLayout->setRowWrapPolicy(QFormLayout::WrapLongRows);
	FMeshCoreTransport = new QComboBox(FMeshCoreFields);
	FMeshCoreTransport->addItem(tr("BLE"), QStringLiteral("ble"));
	FMeshCoreTransport->addItem(tr("USB"), QStringLiteral("usb"));
	FMeshCorePort = new QLineEdit(FMeshCoreFields);
	FMeshCoreBleDevices = new QComboBox(FMeshCoreFields);
	FMeshCoreBleDevices->setObjectName(QStringLiteral("meshcoreBleDevices"));
	FMeshCoreScanBle = new QPushButton(tr("Search"), FMeshCoreFields);
	FMeshCoreScanBle->setObjectName(QStringLiteral("meshcoreScanBleDevices"));
	FMeshCoreBleStatus = new QLabel(tr("Search for nearby MeshCore BLE devices to select one."), FMeshCoreFields);
	FMeshCoreBleStatus->setObjectName(QStringLiteral("meshcoreBleScanStatus"));
	FMeshCoreBleStatus->setWordWrap(true);
	QWidget *bleDeviceRow = new QWidget(FMeshCoreFields);
	QVBoxLayout *bleDeviceLayout = new QVBoxLayout(bleDeviceRow);
	bleDeviceLayout->setContentsMargins(0, 0, 0, 0);
	bleDeviceLayout->setSpacing(3);
	bleDeviceLayout->addWidget(FMeshCoreBleDevices);
	bleDeviceLayout->addWidget(FMeshCoreScanBle, 0, Qt::AlignLeft);

	// Set default values
	FMeshCorePort->setText(QStringLiteral("/dev/ttyACM0"));

	meshcoreLayout->addRow(tr("Transport:"), FMeshCoreTransport);
	meshcoreLayout->addRow(tr("Available MeshCore devices:"), bleDeviceRow);
	meshcoreLayout->addRow(QString(), FMeshCoreBleStatus);
	meshcoreLayout->addRow(tr("USB Port:"), FMeshCorePort);
	connect(FMeshCoreBleDevices, QOverload<int>::of(&QComboBox::activated),
		this, [this](int index) {
			const QString address = FMeshCoreBleDevices->itemData(index).toString();
			if (FMeshCoreMacAddress.select(address))
				emit modified();
		});

#ifndef OS2
	FMeshCoreBleDiscoveryAgent = new QBluetoothDeviceDiscoveryAgent(this);
	FMeshCoreBleDiscoveryAgent->setLowEnergyDiscoveryTimeout(15000);
	connect(FMeshCoreBleDiscoveryAgent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered,
		this, [this](const QBluetoothDeviceInfo &device) {
			const bool isBle = device.coreConfigurations().testFlag(
				QBluetoothDeviceInfo::LowEnergyCoreConfiguration);
			if (!FMeshCoreBleDeviceCatalog.addDevice(device.name(), device.address().toString(), isBle))
				return;
			const QList<MeshCoreBleDevice> devices = FMeshCoreBleDeviceCatalog.devices();
			const MeshCoreBleDevice &candidate = devices.last();
			FMeshCoreBleDevices->addItem(candidate.displayName, candidate.address);
			FMeshCoreBleStatus->setText(tr("Found %n MeshCore BLE device(s); scanning…", "", devices.size()));
		});
	connect(FMeshCoreBleDiscoveryAgent, &QBluetoothDeviceDiscoveryAgent::finished,
		this, [this]() {
			FMeshCoreScanBle->setText(tr("Search"));
			FMeshCoreScanBle->setEnabled(true);
			if (FMeshCoreBleScanFailed)
				return;
			const int count = FMeshCoreBleDeviceCatalog.devices().size();
			FMeshCoreBleStatus->setText(count == 0
				? tr("No MeshCore BLE devices found.")
				: tr("Found %n MeshCore BLE device(s).", "", count));
		});
	connect(FMeshCoreBleDiscoveryAgent, &QBluetoothDeviceDiscoveryAgent::errorOccurred,
		this, [this](QBluetoothDeviceDiscoveryAgent::Error error) {
			if (error == QBluetoothDeviceDiscoveryAgent::NoError)
				return;
			FMeshCoreBleScanFailed = true;
			FMeshCoreBleStatus->setText(FMeshCoreBleDiscoveryAgent->errorString());
			FMeshCoreScanBle->setText(tr("Search"));
			FMeshCoreScanBle->setEnabled(true);
		});
	connect(FMeshCoreScanBle, &QPushButton::clicked, this, [this]() {
		if (!FMeshCoreBleDiscoveryAgent)
			return;
		if (FMeshCoreBleDiscoveryAgent->isActive()) {
			FMeshCoreBleDiscoveryAgent->stop();
			FMeshCoreScanBle->setText(tr("Search"));
			FMeshCoreScanBle->setEnabled(true);
			return;
		}
		FMeshCoreBleDeviceCatalog.clear();
		FMeshCoreBleDevices->clear();
		FMeshCoreBleDevices->addItem(tr("Select a discovered device…"), QString());
		FMeshCoreBleScanFailed = false;
		FMeshCoreBleStatus->setText(tr("Searching for MeshCore BLE devices…"));
		FMeshCoreScanBle->setText(tr("Cancel search"));
		FMeshCoreBleDiscoveryAgent->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
		if (!FMeshCoreBleDiscoveryAgent->isActive() &&
			FMeshCoreBleDiscoveryAgent->error() != QBluetoothDeviceDiscoveryAgent::NoError) {
			FMeshCoreBleScanFailed = true;
			FMeshCoreBleStatus->setText(FMeshCoreBleDiscoveryAgent->errorString());
			FMeshCoreScanBle->setText(tr("Search"));
		}
	});
#else
	FMeshCoreScanBle->setEnabled(false);
	FMeshCoreBleStatus->setText(tr("Bluetooth device discovery is unavailable in this build."));
#endif

	if (QVBoxLayout *root = qobject_cast<QVBoxLayout *>(layout()))
		root->insertWidget(2, FMeshCoreFields);
	FMeshCoreFields->hide();

	// AX.25 KISS serial account configuration.
	FAx25Fields = new QWidget(this);
	QFormLayout *ax25Layout = new QFormLayout(FAx25Fields);
	ax25Layout->setRowWrapPolicy(QFormLayout::WrapLongRows);
	FAx25Callsign = new QLineEdit(FAx25Fields);
	FAx25Callsign->setPlaceholderText(QStringLiteral("DL1AAA-7"));
	FAx25Port = new QLineEdit(FAx25Fields);
	FAx25Port->setPlaceholderText(tr("e.g. /dev/ttyUSB0 or COM3"));
	FAx25BaudRate = new QComboBox(FAx25Fields);
	for (const int baudRate : {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200})
		FAx25BaudRate->addItem(QString::number(baudRate), baudRate);
	FAx25BaudRate->setCurrentIndex(FAx25BaudRate->findData(115200));
	ax25Layout->addRow(tr("Station callsign:"), FAx25Callsign);
	ax25Layout->addRow(tr("KISS serial port:"), FAx25Port);
	ax25Layout->addRow(tr("Baud rate:"), FAx25BaudRate);
	if (QVBoxLayout *root = qobject_cast<QVBoxLayout *>(layout()))
		root->insertWidget(3, FAx25Fields);
	FAx25Fields->hide();

	const auto updateAccountTypeUi = [this](int AIndex) {
		const bool matrix = FAccountType->itemData(AIndex).toString() == QStringLiteral("matrix");
		const bool meshcore = FAccountType->itemData(AIndex).toString() == QStringLiteral("meshcore");
		const bool ax25 = FAccountType->itemData(AIndex).toString() == QStringLiteral("ax25");
		if (QWidget *connectionOptions = parentWidget()->findChild<QWidget *>(QStringLiteral("accountConnectionOptions")))
			connectionOptions->setVisible(!matrix && !meshcore && !ax25);
		ui.grbAccount->setVisible(!matrix && !meshcore && !ax25);
		FMatrixFields->setVisible(matrix);
		FMatrixFields->setEnabled(matrix);
		FMeshCoreFields->setVisible(meshcore);
		FMeshCoreFields->setEnabled(meshcore);
		FAx25Fields->setVisible(ax25);
		FAx25Fields->setEnabled(ax25);
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
	connect(FMeshCorePort,&QLineEdit::textChanged,this,[this](const QString &){ emit modified(); });
	connect(FAx25Callsign, &QLineEdit::textChanged, this, [this](const QString &){ emit modified(); });
	connect(FAx25Port, &QLineEdit::textChanged, this, [this](const QString &){ emit modified(); });
	connect(FAx25BaudRate, QOverload<int>::of(&QComboBox::currentIndexChanged),
		this, [this](int){ emit modified(); });

	reset();
}

AccountOptions::~AccountOptions()
{
#ifndef OS2
	if (FMeshCoreBleDiscoveryAgent && FMeshCoreBleDiscoveryAgent->isActive())
		FMeshCoreBleDiscoveryAgent->stop();
#endif
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
		const bool ax25 = FAccountType->currentData().toString() == QStringLiteral("ax25");
		QString name = ui.lneName->text().trimmed();
		if (name.isEmpty())
			name = matrix ? FMatrixUsername->text().trimmed() :
				(meshcore ? QStringLiteral("@Meshcore") :
				(ax25 ? FAx25Callsign->text().trimmed() : ui.lneJabberId->text().trimmed()));
		if (name.isEmpty())
			name = tr("New Account");

		FAccount->setName(name);
		OptionsNode accountOptions = FAccount->optionsNode();
		accountOptions.setValue(matrix ? QStringLiteral("matrix") :
			(meshcore ? QStringLiteral("meshcore") : (ax25 ? QStringLiteral("ax25") : QStringLiteral("jabber"))), "type");

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
			accountOptions.setValue(FMeshCoreMacAddress.address().trimmed(), "meshcore.mac");
			accountOptions.setValue(FMeshCorePort->text().trimmed(), "meshcore.port");
			FAccount->setPassword(QString()); // Empty password for meshcore
		}
		else if (ax25)
		{
			accountOptions.setValue(QStringLiteral("kiss-serial"), "ax25.transport");
			accountOptions.setValue(FAx25Callsign->text().trimmed().toUpper(), "ax25.callsign");
			accountOptions.setValue(FAx25Port->text().trimmed(), "ax25.port");
			accountOptions.setValue(FAx25BaudRate->currentData().toInt(), "ax25.baud-rate");
			FAccount->setPassword(QString());
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
		else if (ax25 && !FAccount->isValid())
			QMessageBox::warning(this, tr("Invalid AX.25 Account"),
				tr("Account '%1' requires a valid station callsign, serial port, and baud rate.").arg(name));
		else if (!matrix && !meshcore && !ax25 && !FAccount->isValid())
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
		FMeshCoreMacAddress.reset(meshcoreMac);
		if (FMeshCoreBleDevices->count() > 0)
			FMeshCoreBleDevices->setCurrentIndex(0);
		const QString meshcorePort = FAccount->optionsNode().value("meshcore.port").toString();
		FMeshCorePort->setText(meshcorePort.isEmpty()
			? QStringLiteral("/dev/ttyACM0") : meshcorePort);
		FAx25Callsign->setText(FAccount->optionsNode().value("ax25.callsign").toString());
		FAx25Port->setText(FAccount->optionsNode().value("ax25.port").toString());
		const int ax25BaudRate = FAccount->optionsNode().value("ax25.baud-rate").toInt();
		const int ax25BaudIndex = FAx25BaudRate->findData(ax25BaudRate > 0 ? ax25BaudRate : 115200);
		FAx25BaudRate->setCurrentIndex(ax25BaudIndex >= 0 ? ax25BaudIndex : 0);
	}
	emit childReset();
}
