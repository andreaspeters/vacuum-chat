#include "matrixolm.h"
#include "matrixdatabase.h"

#include <QCoreApplication>
#include <QDebug>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <iostream>
#include <stdexcept>

static void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

static void testOutboundMegolmExport()
{
    MatrixOlmCrypto crypto;
    const QString room = QStringLiteral("!synthetic:example.org");
    QString id;
    QByteArray key;
    QByteArray pickle;
    require(crypto.createOutboundMegolmSessionData(room, id, key, pickle), "create outbound Megolm");
    QString exportedId;
    QByteArray exportedKey;
    require(crypto.exportOutboundMegolmSessionWithPickle(room, id, pickle, exportedId, exportedKey),
        "export must preserve initialized outbound Megolm session");
    require(exportedId == id && exportedKey == key, "cached outbound identity/key changed");
    MatrixOlmCrypto restored;
    require(restored.exportOutboundMegolmSessionWithPickle(room, id, pickle, exportedId, exportedKey),
        "export must preserve unpickled outbound Megolm session");
    require(exportedId == id && exportedKey == key, "restored outbound identity/key changed");
    MatrixOlmCrypto invalid;
    require(!invalid.exportOutboundMegolmSessionWithPickle(room, QStringLiteral("wrong-id"), pickle,
        exportedId, exportedKey), "mismatched outbound pickle ID must still be rejected");
}

static void testMegolmRoundtrip()
{
    QTemporaryDir directory;
    require(directory.isValid(), "temporary profile");
    MatrixDatabase database;
    database.setProfileDirectory(directory.path());
    require(database.openForAccount(QStringLiteral("https://example.org"), QStringLiteral("@receiver:example.org")),
        "temporary database");
    MatrixOlmCrypto sender;
    MatrixOlmCrypto receiver;
    const QString room = QStringLiteral("!roundtrip:example.org");
    const QString senderKey = QStringLiteral("synthetic-sender-key");
    QString id;
    QByteArray key;
    require(sender.createOutboundMegolmSession(database, room, id, key), "create persisted outbound");
    require(receiver.importMegolmSession(database, id, room, senderKey, key), "import inbound");
    for (int index = 0; index < 3; ++index) {
        const QByteArray text = QByteArray("synthetic message ") + QByteArray::number(index);
        QString encryptedId = id;
        const QByteArray ciphertext = sender.encryptMegolm(database, room, text, encryptedId);
        require(!ciphertext.isEmpty() && encryptedId == id, "encrypt Megolm");
        const auto result = receiver.decryptMegolmDetailed(database, id, ciphertext);
        require(result.succeeded() && result.plaintext == text && result.messageIndex == uint32_t(index),
            "cached inbound Megolm decrypt must preserve session");
        require(receiver.decryptMegolmDetailedWithPickle(id, ciphertext, QByteArray()).plaintext == text,
            "explicit cached inbound Megolm decrypt");
        require(receiver.persistMegolmSession(database, id, room, senderKey), "persist inbound ratchet");
        MatrixOlmCrypto restoredReceiver;
        require(restoredReceiver.decryptMegolmDetailed(database, id, ciphertext).plaintext == text,
            "restored inbound Megolm decrypt");
        require(sender.outboundMegolmMessageIndexForLoadedSession(room) == index + 1,
            "cached outbound Megolm index must advance");
        require(sender.outboundMegolmMessageIndex(database, room) == index + 1,
            "database-backed outbound Megolm index must advance");
        const QByteArray exported = receiver.exportMegolmSession(database, id, 0);
        MatrixOlmCrypto forwarded;
        QByteArray pickle;
        require(forwarded.prepareExportedMegolmSession(id, exported, pickle), "export/import inbound session");
        require(forwarded.decryptMegolmDetailedWithPickle(id, ciphertext, pickle).plaintext == text,
            "forwarded Megolm decrypt");
    }
}

static QByteArray curveKey(const MatrixOlmCrypto &crypto)
{
    return QJsonDocument::fromJson(crypto.identityKeysJson()).object()
        .value(QStringLiteral("curve25519")).toString().toUtf8();
}

static QByteArray generateOneTimeKey(MatrixOlmCrypto &crypto, const QString &user, const QString &device)
{
    const QJsonObject keys = QJsonDocument::fromJson(crypto.prepareKeysUpload(user, device, 1)).object()
        .value(QStringLiteral("one_time_keys")).toObject();
    require(keys.size() == 1, "generate one synthetic OTK");
    const QByteArray key = keys.begin().value().toObject().value(QStringLiteral("key")).toString().toUtf8();
    crypto.markKeysAsPublished();
    return key;
}

static void testOlmRoundtrip()
{
    QTemporaryDir aliceDirectory;
    QTemporaryDir bobDirectory;
    MatrixDatabase aliceDb;
    MatrixDatabase bobDb;
    const QString aliceUser = QStringLiteral("@alice:example.org");
    const QString bobUser = QStringLiteral("@bob:example.org");
    const QString aliceDevice = QStringLiteral("ALICE");
    const QString bobDevice = QStringLiteral("BOB");
    aliceDb.setProfileDirectory(aliceDirectory.path());
    bobDb.setProfileDirectory(bobDirectory.path());
    require(aliceDb.openForAccount(QStringLiteral("https://example.org"), aliceUser), "Alice database");
    require(bobDb.openForAccount(QStringLiteral("https://example.org"), bobUser), "Bob database");
    MatrixOlmCrypto alice;
    MatrixOlmCrypto bob;
    require(aliceDirectory.isValid() && bobDirectory.isValid(), "temporary Olm profiles");
    require(alice.initialize(aliceDb, aliceUser, aliceDevice), "Alice account");
    require(bob.initialize(bobDb, bobUser, bobDevice), "Bob account");
    const QByteArray bobOtk = generateOneTimeKey(bob, bobUser, bobDevice);
    require(alice.createOlmSession(aliceDb, bobUser, bobDevice, curveKey(bob), bobOtk), "create outbound Olm");
    QString originalId;
    QByteArray originalPickle;
    require(aliceDb.loadOlmSession(bobUser, bobDevice, originalId, originalPickle), "load outbound Olm");
    require(alice.olmSessionId(aliceDb, bobUser, bobDevice) == originalId, "cached Olm ID lookup must not reset session");
    require(alice.olmSessionIdWithPickle(bobUser, bobDevice, originalId, originalPickle) == originalId,
        "cached Olm pickle ID lookup must not reset session");
    int type = -1;
    QByteArray ciphertext = alice.encryptOlm(aliceDb, bobUser, bobDevice, QByteArray("synthetic pre-key"), type);
    require(!ciphertext.isEmpty() && type == 0, "first Olm message must be PRE_KEY");
    require(QByteArray::fromBase64(ciphertext).contains(QByteArray::fromBase64(bobOtk)),
        "PRE_KEY must carry claimed synthetic OTK");
    QString receivedId;
    require(bob.decryptOlm(bobDb, aliceUser, curveKey(alice), type, ciphertext, receivedId, aliceDevice)
        == QByteArray("synthetic pre-key") && receivedId == originalId, "PRE_KEY decrypt and session identity");
    for (int index = 0; index < 3; ++index) {
        const QByteArray reply = QByteArray("synthetic reply ") + QByteArray::number(index);
        ciphertext = bob.encryptOlm(bobDb, aliceUser, aliceDevice, reply, type);
        require(!ciphertext.isEmpty() && type == 1, "Bob normal Olm reply");
        require(alice.decryptOlm(aliceDb, bobUser, curveKey(bob), type, ciphertext, receivedId, bobDevice) == reply,
            "type-1 reply decrypt after outbound encryption");
        if (index == 0) {
            alice.clearOlmSession(bobUser, bobDevice);
            bob.clearOlmSessions();
        }
        const QByteArray text = QByteArray("synthetic followup ") + QByteArray::number(index);
        ciphertext = alice.encryptOlm(aliceDb, bobUser, bobDevice, text, type);
        require(!ciphertext.isEmpty() && type == 1, "Alice normal Olm followup");
        require(bob.decryptOlm(bobDb, aliceUser, curveKey(alice), type, ciphertext, receivedId, aliceDevice) == text,
            "normal Olm followup decrypt after restore");
    }
    require(alice.createOlmSession(aliceDb, bobUser, bobDevice, curveKey(bob),
        generateOneTimeKey(bob, bobUser, bobDevice)), "create alternate persisted Olm candidate");
    ciphertext = bob.encryptOlm(bobDb, aliceUser, aliceDevice, QByteArray("synthetic old-session reply"), type);
    require(alice.decryptOlm(aliceDb, bobUser, curveKey(bob), type, ciphertext, receivedId, bobDevice)
        == QByteArray("synthetic old-session reply") && receivedId == originalId,
        "persisted Olm candidate must decrypt without reinitialization");
    MatrixOlmCrypto restarted;
    require(restarted.initialize(aliceDb, aliceUser, aliceDevice), "restore Alice account");
    ciphertext = bob.encryptOlm(bobDb, aliceUser, aliceDevice, QByteArray("synthetic restart reply"), type);
    require(restarted.decryptOlm(aliceDb, bobUser, curveKey(bob), type, ciphertext, receivedId, bobDevice)
        == QByteArray("synthetic restart reply"), "Olm reply decrypt after process-style restore");
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &message) {
        std::cerr << message.toStdString() << '\n';
    });
    try {
        testOutboundMegolmExport();
        std::cout << "PASS outbound Megolm cached and restored export\n";
        testMegolmRoundtrip();
        std::cout << "PASS Megolm cached/restored/forwarded roundtrip and ratchet index\n";
        testOlmRoundtrip();
        std::cout << "PASS Olm PRE_KEY/type-1 roundtrip, restore and persisted candidates\n";
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
