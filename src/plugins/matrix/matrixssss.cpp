#include "matrixssss.h"

#if HAVE_OPENSSL
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>
#endif

namespace {
#if HAVE_OPENSSL
QByteArray hkdf(const QByteArray &key, const QByteArray &salt, const QByteArray &info)
{
    QByteArray output(64, Qt::Uninitialized);
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
    if (!ctx || EVP_PKEY_derive_init(ctx) <= 0 ||
        EVP_PKEY_CTX_set_hkdf_md(ctx, EVP_sha256()) <= 0 ||
        EVP_PKEY_CTX_set1_hkdf_salt(ctx, reinterpret_cast<const unsigned char *>(salt.constData()), salt.size()) <= 0 ||
        EVP_PKEY_CTX_set1_hkdf_key(ctx, reinterpret_cast<const unsigned char *>(key.constData()), key.size()) <= 0 ||
        EVP_PKEY_CTX_add1_hkdf_info(ctx, reinterpret_cast<const unsigned char *>(info.constData()), info.size()) <= 0) {
        if (ctx)
            EVP_PKEY_CTX_free(ctx);
        return QByteArray();
    }
    size_t length = static_cast<size_t>(output.size());
    const bool ok = EVP_PKEY_derive(ctx, reinterpret_cast<unsigned char *>(output.data()), &length) > 0;
    EVP_PKEY_CTX_free(ctx);
    if (!ok)
        return QByteArray();
    output.resize(static_cast<int>(length));
    return output;
}

QByteArray hmacSha256(const QByteArray &key, const QByteArray &data)
{
    unsigned int length = 0;
    QByteArray result(EVP_MAX_MD_SIZE, Qt::Uninitialized);
    if (!HMAC(EVP_sha256(), key.constData(), key.size(),
              reinterpret_cast<const unsigned char *>(data.constData()), data.size(),
              reinterpret_cast<unsigned char *>(result.data()), &length))
        return QByteArray();
    result.resize(static_cast<int>(length));
    return result;
}

QByteArray aesCtr(const QByteArray &input, const QByteArray &key, const QByteArray &iv)
{
    if (key.size() != 32 || iv.size() != 16)
        return QByteArray();
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx || EVP_EncryptInit_ex(ctx, EVP_aes_256_ctr(), nullptr,
                                   reinterpret_cast<const unsigned char *>(key.constData()),
                                   reinterpret_cast<const unsigned char *>(iv.constData())) != 1) {
        if (ctx)
            EVP_CIPHER_CTX_free(ctx);
        return QByteArray();
    }
    QByteArray output(input.size() + EVP_CIPHER_block_size(EVP_aes_256_ctr()), Qt::Uninitialized);
    int written = 0;
    int finalWritten = 0;
    const bool ok = EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char *>(output.data()), &written,
                                      reinterpret_cast<const unsigned char *>(input.constData()),
                                      input.size()) == 1 &&
        EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char *>(output.data()) + written,
                            &finalWritten) == 1;
    EVP_CIPHER_CTX_free(ctx);
    if (!ok)
        return QByteArray();
    output.resize(written + finalWritten);
    return output;
}

bool verifyKeyDescription(const QByteArray &key, const QJsonObject &keyDescription)
{
    const QByteArray derived = hkdf(key, QByteArray(32, '\0'), QByteArray());
    const QByteArray iv = QByteArray::fromBase64(keyDescription.value(QStringLiteral("iv")).toString().toLatin1());
    const QByteArray expectedMac = QByteArray::fromBase64(keyDescription.value(QStringLiteral("mac")).toString().toLatin1());
    const QByteArray testCiphertext = aesCtr(QByteArray(32, '\0'), derived.left(32), iv);
    return (key.size() == 32 || key.size() == 64) && iv.size() == 16 && derived.size() == 64 &&
        expectedMac == hmacSha256(derived.mid(32, 32), testCiphertext);
}

QByteArray decodeRecoveryKey(const QString &value)
{
    static const QByteArray alphabet("123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz");
    QByteArray bytes(1, '\0');
    int leadingZeros = 0;
    while (leadingZeros < value.size() && value.at(leadingZeros) == QLatin1Char('1'))
        ++leadingZeros;
    for (int index = leadingZeros; index < value.size(); ++index) {
        const QChar character = value.at(index);
        const int digit = alphabet.indexOf(character.toLatin1());
        if (digit < 0)
            return QByteArray();
        int carry = digit;
        for (int i = bytes.size() - 1; i >= 0; --i) {
            const int current = static_cast<unsigned char>(bytes.at(i)) * 58 + carry;
            bytes[i] = static_cast<char>(current & 0xff);
            carry = current >> 8;
        }
        while (carry > 0) {
            bytes.prepend(static_cast<char>(carry & 0xff));
            carry >>= 8;
        }
    }
    int first = 0;
    while (first < bytes.size() - 1 && bytes.at(first) == '\0')
        ++first;
    return QByteArray(leadingZeros, '\0') + bytes.mid(first);
}

#endif
}

bool MatrixSsss::derivePassphraseKey(const QString &passphrase,
                                     const QJsonObject &keyDescription,
                                     QByteArray &key)
{
#if HAVE_OPENSSL
    const QJsonObject passphraseDescription = keyDescription.value(QStringLiteral("passphrase")).toObject();
    if (keyDescription.value(QStringLiteral("algorithm")).toString() !=
            QStringLiteral("m.secret_storage.v1.aes-hmac-sha2") ||
        passphraseDescription.value(QStringLiteral("algorithm")).toString() !=
            QStringLiteral("m.pbkdf2"))
        return false;
    const QByteArray salt = QByteArray::fromBase64(
        passphraseDescription.value(QStringLiteral("salt")).toString().toLatin1());
    const int iterations = passphraseDescription.value(QStringLiteral("iterations")).toInt();
    const int bits = passphraseDescription.contains(QStringLiteral("bits"))
        ? passphraseDescription.value(QStringLiteral("bits")).toInt() : 256;
    if (salt.isEmpty() || iterations <= 0 || (bits != 256 && bits != 512))
        return false;
    key.resize(bits / 8);
    if (PKCS5_PBKDF2_HMAC(passphrase.toUtf8().constData(), passphrase.toUtf8().size(),
                          reinterpret_cast<const unsigned char *>(salt.constData()), salt.size(),
                          iterations, EVP_sha512(), key.size(),
                          reinterpret_cast<unsigned char *>(key.data())) != 1)
        return false;
    return verifyKeyDescription(key, keyDescription);
#else
    Q_UNUSED(passphrase); Q_UNUSED(keyDescription); Q_UNUSED(key);
    return false;
#endif
}

bool MatrixSsss::deriveRecoveryKey(const QString &recoveryKey,
                                   const QJsonObject &keyDescription,
                                   QByteArray &key)
{
#if HAVE_OPENSSL
    QString normalizedRecoveryKey = recoveryKey;
    normalizedRecoveryKey.remove(QLatin1Char(' '));
    normalizedRecoveryKey.remove(QLatin1Char('\n'));
    normalizedRecoveryKey.remove(QLatin1Char('\r'));
    normalizedRecoveryKey.remove(QLatin1Char('\t'));
    const QByteArray decoded = decodeRecoveryKey(normalizedRecoveryKey);
    if (decoded.size() < 3 || static_cast<unsigned char>(decoded.at(0)) != 0x8b ||
        static_cast<unsigned char>(decoded.at(1)) != 0x01)
        return false;
    unsigned char parity = 0;
    for (int i = 0; i < decoded.size() - 1; ++i)
        parity ^= static_cast<unsigned char>(decoded.at(i));
    if (parity != static_cast<unsigned char>(decoded.at(decoded.size() - 1)))
        return false;
    key = decoded.mid(2, decoded.size() - 3);
    return verifyKeyDescription(key, keyDescription);
#else
    Q_UNUSED(recoveryKey); Q_UNUSED(keyDescription); Q_UNUSED(key);
    return false;
#endif
}

bool MatrixSsss::decryptSecret(const QJsonObject &encrypted, const QByteArray &key,
                               const QString &secretName, QByteArray &plaintext)
{
#if HAVE_OPENSSL
    if (key.size() != 32 && key.size() != 64)
        return false;
    const QByteArray ciphertext = QByteArray::fromBase64(encrypted.value(QStringLiteral("ciphertext")).toString().toLatin1());
    const QByteArray iv = QByteArray::fromBase64(encrypted.value(QStringLiteral("iv")).toString().toLatin1());
    const QByteArray mac = QByteArray::fromBase64(encrypted.value(QStringLiteral("mac")).toString().toLatin1());
    const QByteArray derived = hkdf(key, QByteArray(32, '\0'), secretName.toUtf8());
    if (ciphertext.isEmpty() || iv.size() != 16 || derived.size() != 64 ||
        mac != hmacSha256(derived.mid(32, 32), ciphertext))
        return false;
    plaintext = aesCtr(ciphertext, derived.left(32), iv);
    return !plaintext.isEmpty() || ciphertext.isEmpty();
#else
    Q_UNUSED(encrypted); Q_UNUSED(key); Q_UNUSED(secretName); Q_UNUSED(plaintext);
    return false;
#endif
}

bool MatrixSsss::decryptExportedSessions(const QByteArray &fileData,
                                         const QString &passphrase,
                                         QJsonDocument &sessions,
                                         QString &error)
{
#if HAVE_OPENSSL
    const QByteArray begin("-----BEGIN MEGOLM SESSION DATA-----");
    const QByteArray end("-----END MEGOLM SESSION DATA-----");
    const int beginPos = fileData.indexOf(begin);
    const int endPos = fileData.indexOf(end);
    if (beginPos < 0 || endPos <= beginPos) {
        error = QStringLiteral("Invalid Megolm session export markers");
        return false;
    }
    QByteArray encoded = fileData.mid(beginPos + begin.size(), endPos - beginPos - begin.size());
    encoded.replace('\n', QByteArray());
    encoded.replace('\r', QByteArray());
    encoded = encoded.trimmed();
    const QByteArray binary = QByteArray::fromBase64(encoded);
    if (binary.size() < 1 + 16 + 16 + 4 + 32) {
        error = QStringLiteral("Megolm session export is too short");
        return false;
    }
    if (static_cast<unsigned char>(binary.at(0)) != 0x01) {
        error = QStringLiteral("Unsupported Megolm session export version");
        return false;
    }
    const QByteArray salt = binary.mid(1, 16);
    const QByteArray iv = binary.mid(17, 16);
    const int roundsOffset = 33;
    const quint32 rounds = (static_cast<quint32>(static_cast<unsigned char>(binary.at(roundsOffset))) << 24) |
        (static_cast<quint32>(static_cast<unsigned char>(binary.at(roundsOffset + 1))) << 16) |
        (static_cast<quint32>(static_cast<unsigned char>(binary.at(roundsOffset + 2))) << 8) |
        static_cast<quint32>(static_cast<unsigned char>(binary.at(roundsOffset + 3)));
    const int hmacOffset = binary.size() - 32;
    const QByteArray authenticated = binary.left(hmacOffset);
    const QByteArray receivedHmac = binary.right(32);
    QByteArray derived(64, Qt::Uninitialized);
    if (PKCS5_PBKDF2_HMAC(passphrase.toUtf8().constData(), passphrase.toUtf8().size(),
                          reinterpret_cast<const unsigned char *>(salt.constData()), salt.size(),
                          static_cast<int>(rounds), EVP_sha512(), derived.size(),
                          reinterpret_cast<unsigned char *>(derived.data())) != 1) {
        error = QStringLiteral("PBKDF2 derivation failed");
        return false;
    }
    if (hmacSha256(derived.mid(32), authenticated) != receivedHmac) {
        error = QStringLiteral("Megolm session export password or HMAC is invalid");
        return false;
    }
    const QByteArray plaintext = aesCtr(binary.mid(37, hmacOffset - 37), derived.left(32), iv);
    QJsonParseError parseError;
    sessions = QJsonDocument::fromJson(plaintext, &parseError);
    if (parseError.error != QJsonParseError::NoError ||
        (!sessions.isObject() && !sessions.isArray())) {
        error = QStringLiteral("Megolm session export JSON is invalid");
        return false;
    }
    return true;
#else
    Q_UNUSED(fileData); Q_UNUSED(passphrase); Q_UNUSED(sessions);
    error = QStringLiteral("OpenSSL support is unavailable");
    return false;
#endif
}

bool MatrixSsss::encryptSecret(const QByteArray &plaintext, const QByteArray &key,
                               const QString &secretName, QJsonObject &encrypted)
{
#if HAVE_OPENSSL
    if (key.size() != 32)
        return false;
    const QByteArray derived = hkdf(key, QByteArray(32, '\0'), secretName.toUtf8());
    QByteArray iv(16, Qt::Uninitialized);
    if (RAND_bytes(reinterpret_cast<unsigned char *>(iv.data()), iv.size()) != 1)
        return false;
    const QByteArray ciphertext = aesCtr(plaintext, derived.left(32), iv);
    if (ciphertext.isEmpty() && !plaintext.isEmpty())
        return false;
    encrypted = QJsonObject{
        {QStringLiteral("ciphertext"), QString::fromLatin1(ciphertext.toBase64())},
        {QStringLiteral("iv"), QString::fromLatin1(iv.toBase64())},
        {QStringLiteral("mac"), QString::fromLatin1(hmacSha256(derived.mid(32, 32), ciphertext).toBase64())}};
    return true;
#else
    Q_UNUSED(plaintext); Q_UNUSED(key); Q_UNUSED(secretName); Q_UNUSED(encrypted);
    return false;
#endif
}
