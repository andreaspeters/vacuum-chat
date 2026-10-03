#include "meshcorejoindialog.h"

#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>

#include <iostream>

namespace
{
bool check(const char *name, bool condition)
{
    if (condition)
        return true;
    std::cerr << name << " failed\n";
    return false;
}

bool isHex(const QString &value)
{
    if (value.size() != 32)
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

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    bool passed = true;

    MeshCoreJoinDialog existingChats;
    existingChats.setChannels({qMakePair(QStringLiteral("3"), QStringLiteral("RFNet"))});
    existingChats.setContacts({qMakePair(
        QStringLiteral("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"),
        QStringLiteral("Alice"))});
    QListWidget *channels = existingChats.findChild<QListWidget *>(
        QStringLiteral("meshcoreChannelList"));
    QListWidget *contacts = existingChats.findChild<QListWidget *>(
        QStringLiteral("meshcoreContactList"));
    QPushButton *openChannel = existingChats.findChild<QPushButton *>(
        QStringLiteral("meshcoreOpenChannelButton"));
    QPushButton *openContact = existingChats.findChild<QPushButton *>(
        QStringLiteral("meshcoreOpenContactButton"));
    QString openedConversation;
    QObject::connect(&existingChats, &MeshCoreJoinDialog::conversationReady,
                     [&openedConversation](const QString &id) { openedConversation = id; });
    passed &= check("existing channel and contact lists are populated",
                    channels && channels->count() == 1 && contacts && contacts->count() == 1);
    if (channels && openChannel) {
        channels->setCurrentRow(0);
        openChannel->click();
        passed &= check("opening a channel preserves the MeshCore conversation ID",
                        openedConversation == QStringLiteral("channel:3"));
    }
    if (contacts && openContact) {
        contacts->setCurrentRow(0);
        openContact->click();
        passed &= check("opening a contact preserves its full public-key conversation ID",
                        openedConversation == QStringLiteral(
                            "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
    }

    MeshCoreJoinDialog publicChannelDialog;
    QComboBox *channelIndex = publicChannelDialog.findChild<QComboBox *>(
        QStringLiteral("meshcoreChannelIndex"));
    QLineEdit *channelName = publicChannelDialog.findChild<QLineEdit *>(
        QStringLiteral("meshcoreChannelName"));
    QLineEdit *channelKey = publicChannelDialog.findChild<QLineEdit *>(
        QStringLiteral("meshcoreChannelKey"));
    QPushButton *configureChannel = publicChannelDialog.findChild<QPushButton *>(
        QStringLiteral("meshcoreConfigureChannelButton"));
    int requestedChannelIndex = -1;
    QString requestedChannelName;
    QByteArray requestedChannelKey;
    QObject::connect(&publicChannelDialog, &MeshCoreJoinDialog::setChannelRequested,
                     [&](int index, const QString &name, const QByteArray &secret) {
        requestedChannelIndex = index;
        requestedChannelName = name;
        requestedChannelKey = secret;
    });
    passed &= check("public channel key uses the firmware's standard public PSK",
                    channelIndex && channelKey && channelName &&
                    channelIndex->currentData().toInt() == 0 && channelKey->isReadOnly() &&
                    channelKey->text() == QStringLiteral("8b3387e9c5cdea6ac9e5edbaa115cd72"));
    if (configureChannel)
        configureChannel->click();
    passed &= check("public channel request sends its standard 16-byte PSK",
                    requestedChannelIndex == 0 && requestedChannelName == QStringLiteral("Public") &&
                    requestedChannelKey.toHex() == QByteArrayLiteral("8b3387e9c5cdea6ac9e5edbaa115cd72"));
    QString openedPublicChannel;
    QObject::connect(&publicChannelDialog, &MeshCoreJoinDialog::conversationReady,
                     [&openedPublicChannel](const QString &id) { openedPublicChannel = id; });
    publicChannelDialog.setChannelOperationResult(0, true, QString());
    passed &= check("successful channel configuration opens the channel conversation",
                    openedPublicChannel == QStringLiteral("channel:0") &&
                    publicChannelDialog.result() == QDialog::Accepted);

    MeshCoreJoinDialog privateChannelDialog;
    QComboBox *privateChannelIndex = privateChannelDialog.findChild<QComboBox *>(
        QStringLiteral("meshcoreChannelIndex"));
    QLineEdit *privateChannelName = privateChannelDialog.findChild<QLineEdit *>(
        QStringLiteral("meshcoreChannelName"));
    QLineEdit *privateChannelKey = privateChannelDialog.findChild<QLineEdit *>(
        QStringLiteral("meshcoreChannelKey"));
    QPushButton *generateKey = privateChannelDialog.findChild<QPushButton *>(
        QStringLiteral("meshcoreGenerateChannelKeyButton"));
    QPushButton *configurePrivate = privateChannelDialog.findChild<QPushButton *>(
        QStringLiteral("meshcoreConfigureChannelButton"));
    if (privateChannelIndex)
        privateChannelIndex->setCurrentIndex(2);
    if (privateChannelName)
        privateChannelName->setText(QStringLiteral("Private mesh"));
    if (privateChannelKey)
        privateChannelKey->setText(QStringLiteral("00112233445566778899AABBCCDDEEFF"));
    int privateIndex = -1;
    QByteArray privateSecret;
    QObject::connect(&privateChannelDialog, &MeshCoreJoinDialog::setChannelRequested,
                     [&](int index, const QString &, const QByteArray &secret) {
        privateIndex = index;
        privateSecret = secret;
    });
    if (configurePrivate)
        configurePrivate->click();
    passed &= check("private channel key is decoded without altering its bytes",
                    privateIndex == 2 && privateSecret.toHex() ==
                        QByteArrayLiteral("00112233445566778899aabbccddeeff"));

    MeshCoreJoinDialog keyGenerationDialog;
    QComboBox *generationIndex = keyGenerationDialog.findChild<QComboBox *>(
        QStringLiteral("meshcoreChannelIndex"));
    QLineEdit *generatedKey = keyGenerationDialog.findChild<QLineEdit *>(
        QStringLiteral("meshcoreChannelKey"));
    QPushButton *generate = keyGenerationDialog.findChild<QPushButton *>(
        QStringLiteral("meshcoreGenerateChannelKeyButton"));
    if (generationIndex)
        generationIndex->setCurrentIndex(1);
    if (generate)
        generate->click();
    passed &= check("private channel key generation produces 16 random bytes as hex",
                    generatedKey && isHex(generatedKey->text()));

    MeshCoreJoinDialog contactDialog;
    QLineEdit *contactName = contactDialog.findChild<QLineEdit *>(
        QStringLiteral("meshcoreContactName"));
    QLineEdit *contactKey = contactDialog.findChild<QLineEdit *>(
        QStringLiteral("meshcoreContactPublicKey"));
    QPushButton *addContact = contactDialog.findChild<QPushButton *>(
        QStringLiteral("meshcoreAddContactButton"));
    QLabel *contactStatus = contactDialog.findChild<QLabel *>(
        QStringLiteral("meshcoreContactStatus"));
    int contactRequests = 0;
    QString requestedPublicKey;
    QString requestedContactName;
    QObject::connect(&contactDialog, &MeshCoreJoinDialog::addContactRequested,
                     [&](const QString &key, const QString &name) {
        ++contactRequests;
        requestedPublicKey = key;
        requestedContactName = name;
    });
    if (contactName)
        contactName->setText(QStringLiteral("Bob"));
    if (contactKey)
        contactKey->setText(QStringLiteral("bad-key"));
    if (addContact)
        addContact->click();
    passed &= check("malformed contact key is rejected locally",
                    contactRequests == 0 && contactStatus &&
                    contactStatus->text().contains(QStringLiteral("64")));

    const QString bobKey = QStringLiteral(
        "AABBCCDDEEFF00112233445566778899AABBCCDDEEFF00112233445566778899");
    if (contactKey)
        contactKey->setText(bobKey);
    if (addContact)
        addContact->click();
    passed &= check("adding a contact emits the canonical public key and display name",
                    contactRequests == 1 &&
                    requestedPublicKey == bobKey.toLower() &&
                    requestedContactName == QStringLiteral("Bob"));
    QString openedContact;
    QObject::connect(&contactDialog, &MeshCoreJoinDialog::conversationReady,
                     [&openedContact](const QString &id) { openedContact = id; });
    contactDialog.setContactOperationResult(bobKey.toLower(), true, QString());
    passed &= check("successful contact update opens the direct conversation",
                    openedContact == bobKey.toLower() &&
                    contactDialog.result() == QDialog::Accepted);

    return passed ? 0 : 1;
}
