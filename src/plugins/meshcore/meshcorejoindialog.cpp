#include "meshcorejoindialog.h"

#include <QComboBox>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QRandomGenerator>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace
{
const QString PublicChannelKeyHex = QStringLiteral("8b3387e9c5cdea6ac9e5edbaa115cd72");

bool isHexString(const QString &value, int expectedLength)
{
    if (value.size() != expectedLength)
        return false;
    for (const QChar character : value) {
        const QChar lower = character.toLower();
        if (!(lower >= QLatin1Char('0') && lower <= QLatin1Char('9')) &&
            !(lower >= QLatin1Char('a') && lower <= QLatin1Char('f')))
            return false;
    }
    return true;
}
}

MeshCoreJoinDialog::MeshCoreJoinDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Join MeshCore channel / start direct chat"));
    resize(600, 520);

    auto *tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("meshcoreOperationTabs"));

    auto *channelPage = new QWidget(tabs);
    auto *channelLayout = new QVBoxLayout(channelPage);
    auto *channelListGroup = new QGroupBox(tr("Available channels"), channelPage);
    auto *channelListLayout = new QVBoxLayout(channelListGroup);
    FChannels = new QListWidget(channelListGroup);
    FChannels->setObjectName(QStringLiteral("meshcoreChannelList"));
    FOpenChannel = new QPushButton(tr("Open selected channel"), channelListGroup);
    FOpenChannel->setObjectName(QStringLiteral("meshcoreOpenChannelButton"));
    FOpenChannel->setEnabled(false);
    channelListLayout->addWidget(FChannels);
    channelListLayout->addWidget(FOpenChannel, 0, Qt::AlignRight);
    channelLayout->addWidget(channelListGroup, 1);

    auto *hashtagGroup = new QGroupBox(tr("Join a public hashtag channel"), channelPage);
    auto *hashtagForm = new QFormLayout(hashtagGroup);
    FHashtagChannelName = new QLineEdit(hashtagGroup);
    FHashtagChannelName->setObjectName(QStringLiteral("meshcoreHashtagChannelName"));
    FHashtagChannelName->setMaxLength(32);
    FHashtagChannelName->setPlaceholderText(QStringLiteral("#test"));
    auto *hashtagDescription = new QLabel(
        tr("The key is derived from the channel name. Hashtag channels are public, not private."),
        hashtagGroup);
    hashtagDescription->setWordWrap(true);
    FJoinHashtagChannel = new QPushButton(tr("Join public # channel"), hashtagGroup);
    FJoinHashtagChannel->setObjectName(QStringLiteral("meshcoreJoinHashtagChannelButton"));
    FHashtagStatus = new QLabel(hashtagGroup);
    FHashtagStatus->setObjectName(QStringLiteral("meshcoreHashtagChannelStatus"));
    FHashtagStatus->setWordWrap(true);
    hashtagForm->addRow(tr("Channel name"), FHashtagChannelName);
    hashtagForm->addRow(QString(), hashtagDescription);
    hashtagForm->addRow(QString(), FJoinHashtagChannel);
    hashtagForm->addRow(QString(), FHashtagStatus);
    channelLayout->addWidget(hashtagGroup);

    auto *channelFormGroup = new QGroupBox(tr("Configure a channel"), channelPage);
    auto *channelForm = new QFormLayout(channelFormGroup);
    FChannelIndex = new QComboBox(channelFormGroup);
    FChannelIndex->setObjectName(QStringLiteral("meshcoreChannelIndex"));
    for (int index = 0; index < 8; ++index) {
        const QString label = index == 0
            ? tr("0 — Public channel") : QString::number(index);
        FChannelIndex->addItem(label, index);
    }
    FChannelName = new QLineEdit(channelFormGroup);
    FChannelName->setObjectName(QStringLiteral("meshcoreChannelName"));
    FChannelName->setMaxLength(32);
    FChannelName->setText(tr("Public"));

    auto *keyRow = new QWidget(channelFormGroup);
    auto *keyLayout = new QHBoxLayout(keyRow);
    keyLayout->setContentsMargins(0, 0, 0, 0);
    FChannelKey = new QLineEdit(keyRow);
    FChannelKey->setObjectName(QStringLiteral("meshcoreChannelKey"));
    FChannelKey->setEchoMode(QLineEdit::Password);
    FChannelKey->setMaxLength(32);
    FChannelKey->setText(PublicChannelKeyHex);
    FChannelKey->setReadOnly(true);
    FGenerateKey = new QPushButton(tr("Generate"), keyRow);
    FGenerateKey->setObjectName(QStringLiteral("meshcoreGenerateChannelKeyButton"));
    FGenerateKey->setEnabled(false);
    keyLayout->addWidget(FChannelKey, 1);
    keyLayout->addWidget(FGenerateKey);

    FConfigureChannel = new QPushButton(tr("Join / update channel"), channelFormGroup);
    FConfigureChannel->setObjectName(QStringLiteral("meshcoreConfigureChannelButton"));
    FChannelStatus = new QLabel(channelFormGroup);
    FChannelStatus->setObjectName(QStringLiteral("meshcoreChannelStatus"));
    FChannelStatus->setWordWrap(true);
    channelForm->addRow(tr("Slot"), FChannelIndex);
    channelForm->addRow(tr("Channel name"), FChannelName);
    channelForm->addRow(tr("Channel key (32 hex digits)"), keyRow);
    channelForm->addRow(QString(), FConfigureChannel);
    channelForm->addRow(QString(), FChannelStatus);
    channelLayout->addWidget(channelFormGroup);
    tabs->addTab(channelPage, tr("Channels"));

    auto *contactPage = new QWidget(tabs);
    auto *contactLayout = new QVBoxLayout(contactPage);
    auto *contactListGroup = new QGroupBox(tr("Contacts"), contactPage);
    auto *contactListLayout = new QVBoxLayout(contactListGroup);
    FContacts = new QListWidget(contactListGroup);
    FContacts->setObjectName(QStringLiteral("meshcoreContactList"));
    FOpenContact = new QPushButton(tr("Open selected direct chat"), contactListGroup);
    FOpenContact->setObjectName(QStringLiteral("meshcoreOpenContactButton"));
    FOpenContact->setEnabled(false);
    contactListLayout->addWidget(FContacts);
    contactListLayout->addWidget(FOpenContact, 0, Qt::AlignRight);
    contactLayout->addWidget(contactListGroup, 1);

    auto *contactFormGroup = new QGroupBox(tr("Add a contact by public key"), contactPage);
    auto *contactForm = new QFormLayout(contactFormGroup);
    FContactName = new QLineEdit(contactFormGroup);
    FContactName->setObjectName(QStringLiteral("meshcoreContactName"));
    FContactName->setMaxLength(31);
    FContactPublicKey = new QLineEdit(contactFormGroup);
    FContactPublicKey->setObjectName(QStringLiteral("meshcoreContactPublicKey"));
    FContactPublicKey->setMaxLength(64);
    FContactPublicKey->setPlaceholderText(tr("64 hexadecimal characters"));
    FAddContact = new QPushButton(tr("Add contact / start chat"), contactFormGroup);
    FAddContact->setObjectName(QStringLiteral("meshcoreAddContactButton"));
    FContactStatus = new QLabel(contactFormGroup);
    FContactStatus->setObjectName(QStringLiteral("meshcoreContactStatus"));
    FContactStatus->setWordWrap(true);
    contactForm->addRow(tr("Display name"), FContactName);
    contactForm->addRow(tr("Public key"), FContactPublicKey);
    contactForm->addRow(QString(), FAddContact);
    contactForm->addRow(QString(), FContactStatus);
    contactLayout->addWidget(contactFormGroup);
    tabs->addTab(contactPage, tr("Contacts"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
    layout->addWidget(buttons);

    connect(FConfigureChannel, &QPushButton::clicked,
            this, &MeshCoreJoinDialog::configureChannel);
    connect(FJoinHashtagChannel, &QPushButton::clicked,
            this, &MeshCoreJoinDialog::joinHashtagChannel);
    connect(FGenerateKey, &QPushButton::clicked, this, [this]() {
        QByteArray key;
        key.reserve(16);
        for (int wordIndex = 0; wordIndex < 4; ++wordIndex) {
            const quint32 word = QRandomGenerator::system()->generate();
            for (int byteIndex = 0; byteIndex < 4; ++byteIndex)
                key.append(static_cast<char>((word >> (byteIndex * 8)) & 0xffu));
        }
        FChannelKey->setText(QString::fromLatin1(key.toHex()));
        setStatus(FChannelStatus, tr("A new key was generated. It is sent only to the radio."), false);
    });
    connect(FAddContact, &QPushButton::clicked,
            this, &MeshCoreJoinDialog::addContact);
    connect(FOpenChannel, &QPushButton::clicked,
            this, &MeshCoreJoinDialog::openSelectedChannel);
    connect(FOpenContact, &QPushButton::clicked,
            this, &MeshCoreJoinDialog::openSelectedContact);
    connect(FChannels, &QListWidget::currentRowChanged, this, [this](int row) {
        FOpenChannel->setEnabled(row >= 0 && FPendingAction == NoPendingAction);
    });
    connect(FContacts, &QListWidget::currentRowChanged, this, [this](int row) {
        FOpenContact->setEnabled(row >= 0 && FPendingAction == NoPendingAction);
    });
    connect(FChannels, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *) { openSelectedChannel(); });
    connect(FContacts, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *) { openSelectedContact(); });
    connect(FChannelIndex, SIGNAL(currentIndexChanged(int)),
            this, SLOT(channelSlotChanged(int)));
}

void MeshCoreJoinDialog::setChannels(const QList<QPair<QString, QString>> &channels)
{
    const QString selectedId = FChannels->currentItem()
        ? FChannels->currentItem()->data(Qt::UserRole).toString() : QString();
    FChannels->clear();
    for (const QPair<QString, QString> &channel : channels) {
        bool ok = false;
        const int index = channel.first.toInt(&ok);
        if (!ok || index < 0 || index > 7)
            continue;
        const QString name = channel.second.isEmpty()
            ? tr("Channel %1").arg(index) : channel.second;
        auto *item = new QListWidgetItem(name, FChannels);
        item->setData(Qt::UserRole, channel.first);
        item->setData(Qt::UserRole + 1, channel.second);
        item->setToolTip(tr("Channel slot %1").arg(index));
        if (channel.first == selectedId)
            FChannels->setCurrentItem(item);
    }
}

void MeshCoreJoinDialog::setAvailableChannelSlots(const QList<int> &availableSlots)
{
    FAvailableChannelSlots.clear();
    for (int index : availableSlots) {
        if (index > 0 && index < 8 && !FAvailableChannelSlots.contains(index))
            FAvailableChannelSlots.append(index);
    }
}

void MeshCoreJoinDialog::setContacts(const QList<QPair<QString, QString>> &contacts)
{
    const QString selectedId = FContacts->currentItem()
        ? FContacts->currentItem()->data(Qt::UserRole).toString() : QString();
    FContacts->clear();
    for (const QPair<QString, QString> &contact : contacts) {
        const QString displayName = contact.second.isEmpty()
            ? contact.first.left(12) : contact.second;
        auto *item = new QListWidgetItem(
            tr("%1 — %2").arg(displayName, contact.first.left(12)), FContacts);
        item->setData(Qt::UserRole, contact.first.toLower());
        item->setToolTip(contact.first);
        if (contact.first.compare(selectedId, Qt::CaseInsensitive) == 0)
            FContacts->setCurrentItem(item);
    }
}

void MeshCoreJoinDialog::setChannelOperationResult(int channelIndex, bool success,
                                                   const QString &error)
{
    if (FPendingAction != ConfigureChannel || FPendingChannelIndex != channelIndex)
        return;

    QLabel *status = FPendingHashtagChannel ? FHashtagStatus : FChannelStatus;
    setPending(false);
    FPendingAction = NoPendingAction;
    FPendingChannelIndex = -1;
    FPendingHashtagChannel = false;
    if (!success) {
        setStatus(status,
                  error.isEmpty() ? tr("The channel could not be configured.") : error, true);
        return;
    }

    setStatus(status, tr("Channel configured."), false);
    emit conversationReady(QStringLiteral("channel:%1").arg(channelIndex));
    accept();
}

void MeshCoreJoinDialog::setContactOperationResult(const QString &publicKeyHex, bool success,
                                                   const QString &error)
{
    if (FPendingAction != AddContact ||
        FPendingContactPublicKey.compare(publicKeyHex, Qt::CaseInsensitive) != 0)
        return;

    const QString conversationId = FPendingContactPublicKey;
    setPending(false);
    FPendingAction = NoPendingAction;
    FPendingContactPublicKey.clear();
    if (!success) {
        setStatus(FContactStatus,
                  error.isEmpty() ? tr("The contact could not be added.") : error, true);
        return;
    }

    setStatus(FContactStatus, tr("Contact added."), false);
    emit conversationReady(conversationId);
    accept();
}

void MeshCoreJoinDialog::configureChannel()
{
    if (FPendingAction != NoPendingAction)
        return;

    const int channelIndex = FChannelIndex->currentData().toInt();
    const QString name = FChannelName->text();
    if (name.trimmed().isEmpty() || name.contains(QChar::Null) ||
        name.toUtf8().size() > 32) {
        setStatus(FChannelStatus, tr("Enter a channel name of at most 32 UTF-8 bytes."), true);
        return;
    }

    const QString keyHex = FChannelKey->text().trimmed();
    if (!isHexString(keyHex, 32)) {
        setStatus(FChannelStatus, tr("Enter a 16-byte key as exactly 32 hexadecimal digits."), true);
        return;
    }
    const QByteArray secret = QByteArray::fromHex(keyHex.toLatin1());
    if (secret.size() != 16) {
        setStatus(FChannelStatus, tr("The channel key is invalid."), true);
        return;
    }

    FPendingAction = ConfigureChannel;
    FPendingChannelIndex = channelIndex;
    FPendingHashtagChannel = false;
    setPending(true);
    setStatus(FChannelStatus, tr("Sending channel configuration to the radio…"), false);
    emit setChannelRequested(channelIndex, name, secret);
}

void MeshCoreJoinDialog::joinHashtagChannel()
{
    if (FPendingAction != NoPendingAction)
        return;

    const QString name = FHashtagChannelName->text().trimmed();
    const QByteArray encodedName = name.toUtf8();
    if (!name.startsWith(QLatin1Char('#')) || name.size() <= 1 ||
        name.contains(QChar::Null) || encodedName.size() > 32) {
        setStatus(FHashtagStatus,
                  tr("Enter a hashtag channel beginning with # and no more than 32 UTF-8 bytes."),
                  true);
        return;
    }

    for (int row = 0; row < FChannels->count(); ++row) {
        const QListWidgetItem *item = FChannels->item(row);
        if (item->data(Qt::UserRole + 1).toString() == name) {
            emit conversationReady(QStringLiteral("channel:") +
                                   item->data(Qt::UserRole).toString());
            accept();
            return;
        }
    }

    int channelIndex = -1;
    for (int candidate = 1; candidate < 8 && channelIndex < 0; ++candidate) {
        if (FAvailableChannelSlots.contains(candidate))
            channelIndex = candidate;
    }
    if (channelIndex < 0) {
        setStatus(FHashtagStatus, tr("All hashtag channel slots are occupied."), true);
        return;
    }

    const QByteArray secret = QCryptographicHash::hash(
        encodedName, QCryptographicHash::Sha256).left(16);
    FPendingAction = ConfigureChannel;
    FPendingChannelIndex = channelIndex;
    FPendingHashtagChannel = true;
    setPending(true);
    setStatus(FHashtagStatus, tr("Adding the public hashtag channel to the radio…"), false);
    emit setChannelRequested(channelIndex, name, secret);
}

void MeshCoreJoinDialog::addContact()
{
    if (FPendingAction != NoPendingAction)
        return;

    const QString publicKey = FContactPublicKey->text().trimmed();
    if (!isHexString(publicKey, 64)) {
        setStatus(FContactStatus, tr("Enter a public key as exactly 64 hexadecimal characters."), true);
        return;
    }

    const QString name = FContactName->text();
    if (name.trimmed().isEmpty() || name.contains(QChar::Null) ||
        name.toUtf8().size() > 31) {
        setStatus(FContactStatus, tr("Enter a contact name of at most 31 UTF-8 bytes."), true);
        return;
    }

    FPendingAction = AddContact;
    FPendingContactPublicKey = publicKey.toLower();
    setPending(true);
    setStatus(FContactStatus, tr("Adding the contact to the radio…"), false);
    emit addContactRequested(FPendingContactPublicKey, name);
}

void MeshCoreJoinDialog::openSelectedChannel()
{
    if (FPendingAction != NoPendingAction || !FChannels->currentItem())
        return;
    const QString index = FChannels->currentItem()->data(Qt::UserRole).toString();
    emit conversationReady(QStringLiteral("channel:") + index);
    accept();
}

void MeshCoreJoinDialog::openSelectedContact()
{
    if (FPendingAction != NoPendingAction || !FContacts->currentItem())
        return;
    emit conversationReady(FContacts->currentItem()->data(Qt::UserRole).toString());
    accept();
}

void MeshCoreJoinDialog::channelSlotChanged(int comboIndex)
{
    const int channelIndex = FChannelIndex->itemData(comboIndex).toInt();
    if (channelIndex == 0) {
        FChannelKey->setText(PublicChannelKeyHex);
        FChannelKey->setReadOnly(true);
        FGenerateKey->setEnabled(false);
        if (FCurrentChannelIndex != 0 && FChannelName->text().isEmpty())
            FChannelName->setText(tr("Public"));
    } else {
        if (FCurrentChannelIndex == 0 && FChannelKey->text() == PublicChannelKeyHex)
            FChannelKey->clear();
        if (FCurrentChannelIndex == 0 && FChannelName->text() == tr("Public"))
            FChannelName->clear();
        FChannelKey->setReadOnly(false);
        FGenerateKey->setEnabled(FPendingAction == NoPendingAction);
    }
    FCurrentChannelIndex = channelIndex;
}

void MeshCoreJoinDialog::setStatus(QLabel *label, const QString &text, bool error)
{
    label->setText(text);
    label->setStyleSheet(error ? QStringLiteral("color: #b00020;") : QString());
}

void MeshCoreJoinDialog::setPending(bool pending)
{
    FConfigureChannel->setEnabled(!pending);
    FHashtagChannelName->setEnabled(!pending);
    FJoinHashtagChannel->setEnabled(!pending);
    FAddContact->setEnabled(!pending);
    FGenerateKey->setEnabled(!pending && FChannelIndex->currentData().toInt() != 0);
    FOpenChannel->setEnabled(!pending && FChannels->currentRow() >= 0);
    FOpenContact->setEnabled(!pending && FContacts->currentRow() >= 0);
}
