#include "matrixnetwork.h"
#include "matrixdatabase.h"
#include "matrixeventcontentmetadata.h"

#include <QBuffer>
#include <QColor>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QEventLoop>
#include <QHash>
#include <QHostAddress>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QSize>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include <openssl/evp.h>

#include <iostream>

class MatrixNetworkTestAccess
{
public:
    static void setSession(MatrixNetwork &network, const QString &userId, const QString &token)
    {
        network.FUserId = userId;
        network.FAccesToken = token;
    }
};

namespace {
class MediaTestServer final : public QTcpServer
{
public:
    QByteArray responseBody;
    QByteArray requestTarget;
    QByteArray authorization;

    bool start()
    {
        connect(this, &QTcpServer::newConnection, this, [this]() {
            while (hasPendingConnections()) {
                QTcpSocket *socket = nextPendingConnection();
                connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { process(socket); });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                if (socket->bytesAvailable() > 0)
                    process(socket);
            }
        });
        return listen(QHostAddress::LocalHost, 0);
    }

    QUrl baseUrl() const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(serverPort()));
    }

private:
    void process(QTcpSocket *socket)
    {
        QByteArray &request = FRequests[socket];
        request += socket->readAll();
        const int headerEnd = request.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;
        const QList<QByteArray> lines = request.left(headerEnd).split('\n');
        const QList<QByteArray> requestLine = lines.value(0).trimmed().split(' ');
        if (requestLine.size() < 2) {
            socket->abort();
            FRequests.remove(socket);
            return;
        }
        requestTarget = requestLine.at(1);
        for (int i = 1; i < lines.size(); ++i) {
            const QByteArray header = lines.at(i).trimmed();
            const int colon = header.indexOf(':');
            if (colon > 0 && header.left(colon).trimmed().toLower() == QByteArrayLiteral("authorization"))
                authorization = header.mid(colon + 1).trimmed();
        }
        FRequests.remove(socket);
        QByteArray response = QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n");
        response += QByteArrayLiteral("Content-Length: ") + QByteArray::number(responseBody.size()) +
            QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + responseBody;
        socket->write(response);
        socket->disconnectFromHost();
    }

    QHash<QTcpSocket *, QByteArray> FRequests;
};

QByteArray encodeBase64Url(const QByteArray &bytes)
{
    return bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

QByteArray encryptAesCtr(const QByteArray &plain, const QByteArray &key, const QByteArray &iv)
{
    EVP_CIPHER_CTX *context = EVP_CIPHER_CTX_new();
    if (!context || EVP_EncryptInit_ex(context, EVP_aes_256_ctr(), nullptr,
        reinterpret_cast<const unsigned char *>(key.constData()),
        reinterpret_cast<const unsigned char *>(iv.constData())) != 1) {
        if (context)
            EVP_CIPHER_CTX_free(context);
        return QByteArray();
    }
    QByteArray encrypted(plain.size() + EVP_CIPHER_block_size(EVP_aes_256_ctr()), Qt::Uninitialized);
    int produced = 0;
    int finalProduced = 0;
    const bool ok = EVP_EncryptUpdate(context, reinterpret_cast<unsigned char *>(encrypted.data()),
        &produced, reinterpret_cast<const unsigned char *>(plain.constData()),
        static_cast<int>(plain.size())) == 1 &&
        EVP_EncryptFinal_ex(context, reinterpret_cast<unsigned char *>(encrypted.data()) + produced,
            &finalProduced) == 1;
    EVP_CIPHER_CTX_free(context);
    if (!ok)
        return QByteArray();
    encrypted.resize(produced + finalProduced);
    return encrypted;
}

QByteArray syntheticPng()
{
    QImage image(8, 6, QImage::Format_ARGB32);
    image.fill(QColor(24, 180, 90));
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
        return QByteArray();
    return bytes;
}

bool check(bool condition, const char *description)
{
    if (!condition)
        std::cerr << description << '\n';
    return condition;
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir cacheDirectory;
    if (!check(cacheDirectory.isValid(), "temporary Matrix media cache could not be created"))
        return 1;

    MediaTestServer server;
    if (!check(server.start(), "local Matrix media HTTP server did not start"))
        return 1;

    const QByteArray plainImage = syntheticPng();
    const QByteArray key(32, '\x31');
    const QByteArray iv(16, '\x72');
    const QByteArray ciphertext = encryptAesCtr(plainImage, key, iv);
    if (!check(!plainImage.isEmpty() && !ciphertext.isEmpty(), "synthetic encrypted image fixture could not be created"))
        return 1;
    server.responseBody = ciphertext;
    const QString mediaId = QStringLiteral("encrypted-image-%1")
        .arg(QCoreApplication::applicationPid());
    const QString eventId = QLatin1Char('$') + mediaId + QStringLiteral(":example.org");

    QJsonObject keyDescription{
        {QStringLiteral("kty"), QStringLiteral("oct")},
        {QStringLiteral("key_ops"), QJsonArray{QStringLiteral("encrypt"), QStringLiteral("decrypt")}},
        {QStringLiteral("alg"), QStringLiteral("A256CTR")},
        {QStringLiteral("k"), QString::fromLatin1(encodeBase64Url(key))},
        {QStringLiteral("ext"), true}};
    QJsonObject encryptedFile{
        {QStringLiteral("url"), QStringLiteral("mxc://media.example.org/%1").arg(mediaId)},
        {QStringLiteral("key"), keyDescription},
        {QStringLiteral("iv"), QString::fromLatin1(encodeBase64Url(iv))},
        {QStringLiteral("hashes"), QJsonObject{
            {QStringLiteral("sha256"), QString::fromLatin1(encodeBase64Url(
                QCryptographicHash::hash(ciphertext, QCryptographicHash::Sha256)))}}}};

    const QString roomId = QStringLiteral("!encrypted-image:example.org");
    const QString userId = QStringLiteral("@image-test:example.org");
    MatrixDatabase database;
    database.setProfileDirectory(cacheDirectory.path());
    if (!check(database.openForAccount(QStringLiteral("https://example.org"), userId),
            "temporary Matrix media database could not be opened"))
        return 1;

    MatrixTimelineEvent pendingEvent;
    pendingEvent.roomId = roomId;
    pendingEvent.eventId = eventId;
    pendingEvent.eventType = QStringLiteral("m.room.encrypted");
    pendingEvent.sender = QStringLiteral("@sender:example.org");
    pendingEvent.originTs = 1700000000000LL;
    pendingEvent.messageType = QStringLiteral("m.encrypted");
    pendingEvent.content = QStringLiteral("[encrypted Matrix event]");
    pendingEvent.metadata.insert(QStringLiteral("session_id"), QStringLiteral("synthetic-session"));
    pendingEvent.metadata.insert(QStringLiteral("ciphertext"), QStringLiteral("synthetic-ciphertext"));
    if (!check(database.appendTimelineEvents(roomId, {pendingEvent}),
            "pending encrypted image event could not be stored"))
        return 1;

    const QJsonObject decryptedContent{
        {QStringLiteral("msgtype"), QStringLiteral("m.image")},
        {QStringLiteral("body"), QStringLiteral("encrypted.png")},
        {QStringLiteral("file"), encryptedFile},
        {QStringLiteral("info"), QJsonObject{
            {QStringLiteral("mimetype"), QStringLiteral("image/png")},
            {QStringLiteral("thumbnail_file"), encryptedFile}}}};
    QVariantMap decryptedMetadata;
    MatrixEventContentMetadata::copyMediaFields(decryptedContent, decryptedMetadata);
    MatrixTimelineEvent decryptedEvent = pendingEvent;
    decryptedEvent.eventType = QStringLiteral("m.room.message");
    decryptedEvent.messageType = QStringLiteral("m.image");
    decryptedEvent.content = QStringLiteral("encrypted.png");
    for (auto it = decryptedMetadata.cbegin(); it != decryptedMetadata.cend(); ++it)
        decryptedEvent.metadata.insert(it.key(), it.value());
    decryptedEvent.metadata.insert(QStringLiteral("decryption_status"), QStringLiteral("decrypted"));
    if (!check(database.updateTimelineEvent(decryptedEvent),
            "decrypted image event could not replace its pending timeline row"))
        return 1;

    database.close();
    if (!check(database.openForAccount(QStringLiteral("https://example.org"), userId),
            "Matrix media database could not be reopened"))
        return 1;
    const QList<MatrixTimelineEvent> persistedEvents = database.getEvents(roomId);
    if (!check(persistedEvents.size() == 1,
            "reopened Matrix media database did not return the decrypted event"))
        return 1;
    const QVariantMap persistedFile = persistedEvents.first().metadata
        .value(QStringLiteral("file")).toMap();
    if (!check(persistedFile.value(QStringLiteral("url")).toString() ==
            encryptedFile.value(QStringLiteral("url")).toString() &&
            persistedFile.value(QStringLiteral("key")).toMap().value(QStringLiteral("k")).toString() ==
            keyDescription.value(QStringLiteral("k")).toString() &&
            persistedEvents.first().metadata.value(QStringLiteral("info")).toMap()
                .value(QStringLiteral("thumbnail_file")).toMap()
                .value(QStringLiteral("url")).toString() ==
            encryptedFile.value(QStringLiteral("url")).toString(),
            "encrypted file descriptor and thumbnail metadata did not survive database reopen"))
        return 1;

    MatrixNetwork network;
    network.setServerUrl(server.baseUrl().toString());
    network.setDatabaseProfileDirectory(cacheDirectory.path());
    MatrixNetworkTestAccess::setSession(network, userId,
        QStringLiteral("synthetic-token"));

    bool receivedDecodedImage = false;
    QImage receivedImage;
    QEventLoop completionLoop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&network, &MatrixNetwork::messageReceived, &completionLoop,
        [&receivedDecodedImage, &receivedImage, &completionLoop, eventId](const BasicMessage &message) {
            if (message.messageId() != eventId)
                return;
            receivedImage = message.metadata().value(QStringLiteral("decoded_image")).value<QImage>();
            receivedDecodedImage = !receivedImage.isNull();
            if (receivedDecodedImage)
                completionLoop.quit();
        });
    QObject::connect(&timeout, &QTimer::timeout, &completionLoop, &QEventLoop::quit);

    network.requestHistoricalImages(roomId, persistedEvents);
    timeout.start(5000);
    completionLoop.exec();

    bool passed = true;
    const QByteArray expectedPath = QByteArrayLiteral("/download/media.example.org/") + mediaId.toLatin1();
    passed &= check(server.requestTarget.contains(expectedPath),
        "encrypted Matrix image file.url was not requested through the media API");
    passed &= check(server.authorization == QByteArrayLiteral("Bearer synthetic-token"),
        "encrypted Matrix media request did not carry the authenticated session");
    passed &= check(receivedDecodedImage, "encrypted Matrix image was not decrypted and decoded");
    if (receivedDecodedImage) {
        passed &= check(receivedImage.size() == QSize(8, 6), "decrypted Matrix image dimensions changed");
        passed &= check(receivedImage.pixelColor(0, 0) == QColor(24, 180, 90),
            "decrypted Matrix image pixels do not match the source fixture");
    }
    return passed ? 0 : 1;
}
