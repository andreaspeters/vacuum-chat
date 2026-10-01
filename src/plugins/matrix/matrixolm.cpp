#include "matrixolm.h"

#include "matrixdatabase.h"

#include <QRandomGenerator>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <algorithm>
#include <cstring>
#include <vector>

#if HAVE_OLM
#include <olm/olm.h>
#include <olm/pk.h>
#include <olm/sas.h>
#endif

#if HAVE_OLM
static QString inboundMegolmSessionId(OlmInboundGroupSession *session)
{
    if (!session)
        return QString();
    const size_t length = olm_inbound_group_session_id_length(session);
    QByteArray id(static_cast<int>(length), Qt::Uninitialized);
    if (olm_inbound_group_session_id(session, reinterpret_cast<uint8_t *>(id.data()), length) == olm_error())
        return QString();
    return QString::fromUtf8(id);
}

static QString outboundMegolmSessionId(OlmOutboundGroupSession *session)
{
    if (!session)
        return QString();
    const size_t length = olm_outbound_group_session_id_length(session);
    QByteArray id(static_cast<int>(length), Qt::Uninitialized);
    if (olm_outbound_group_session_id(session, reinterpret_cast<uint8_t *>(id.data()), length) == olm_error())
        return QString();
    return QString::fromUtf8(id);
}
#endif

static const QStringList MatrixSasEmoji = {
    QStringLiteral("🐶"), QStringLiteral("🐱"), QStringLiteral("🦁"), QStringLiteral("🐎"),
    QStringLiteral("🦄"), QStringLiteral("🐷"), QStringLiteral("🐘"), QStringLiteral("🐰"),
    QStringLiteral("🐼"), QStringLiteral("🐓"), QStringLiteral("🐧"), QStringLiteral("🐢"),
    QStringLiteral("🐟"), QStringLiteral("🐙"), QStringLiteral("🦋"), QStringLiteral("🌷"),
    QStringLiteral("🌳"), QStringLiteral("🌵"), QStringLiteral("🍄"), QStringLiteral("🌏"),
    QStringLiteral("🌙"), QStringLiteral("☁️"), QStringLiteral("🔥"), QStringLiteral("🍌"),
    QStringLiteral("🍎"), QStringLiteral("🍓"), QStringLiteral("🌽"), QStringLiteral("🍕"),
    QStringLiteral("🎂"), QStringLiteral("❤️"), QStringLiteral("😀"), QStringLiteral("🤖"),
    QStringLiteral("🎩"), QStringLiteral("👓"), QStringLiteral("🔧"), QStringLiteral("🎅"),
    QStringLiteral("👍"), QStringLiteral("☂️"), QStringLiteral("⌛"), QStringLiteral("⏰"),
    QStringLiteral("🎁"), QStringLiteral("💡"), QStringLiteral("📕"), QStringLiteral("✏️"),
    QStringLiteral("📎"), QStringLiteral("✂️"), QStringLiteral("🔒"), QStringLiteral("🔑"),
    QStringLiteral("🔨"), QStringLiteral("☎️"), QStringLiteral("🏁"), QStringLiteral("🚂"),
    QStringLiteral("🚲"), QStringLiteral("✈️"), QStringLiteral("🚀"), QStringLiteral("🏆"),
    QStringLiteral("⚽"), QStringLiteral("🎸"), QStringLiteral("🎺"), QStringLiteral("🔔"),
    QStringLiteral("⚓"), QStringLiteral("🎧"), QStringLiteral("📁"), QStringLiteral("📌")
};

static QByteArray canonicalJsonValue(const QJsonValue &value)
{
	if (value.isObject()) {
		QStringList keys = value.toObject().keys();
		std::sort(keys.begin(), keys.end());
		QByteArray result("{");
		for (int i = 0; i < keys.size(); ++i) {
			if (i > 0)
				result.append(',');
			const QByteArray key = QJsonDocument(QJsonArray{keys.at(i)}).toJson(QJsonDocument::Compact);
			result.append(key.mid(1, key.size() - 2));
			result.append(':');
			result.append(canonicalJsonValue(value.toObject().value(keys.at(i))));
		}
		result.append('}');
		return result;
	}
	if (value.isArray()) {
		QByteArray result("[");
		const QJsonArray array = value.toArray();
		for (int i = 0; i < array.size(); ++i) {
			if (i > 0)
				result.append(',');
			result.append(canonicalJsonValue(array.at(i)));
		}
		result.append(']');
		return result;
	}
	const QByteArray encoded = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
	return encoded.mid(1, encoded.size() - 2);
}

MatrixOlmCrypto::MatrixOlmCrypto()
    : FAccount(nullptr)
{
}

MatrixOlmCrypto::~MatrixOlmCrypto()
{
    clear();
}

void MatrixOlmCrypto::clear()
{
#if HAVE_OLM
    if (FAccount) {
        QByteArray accountBytes(static_cast<const char *>(FAccount),
                                static_cast<int>(olm_account_size()));
        accountBytes.fill('\0');
        delete [] static_cast<char *>(FAccount);
    }
#endif
    FAccount = nullptr;
    FPickleKey.fill('\0');
    FPickleKey.clear();
    FMegolmSessions.clear();
    FOutboundMegolmPickles.clear();
    FOutboundMegolmSessions.clear();
    FOutboundMegolmSessionIds.clear();
    FOutboundMegolmSessionKeys.clear();
    FSasSessions.clear();
    FSasMacMethods.clear();
    FOlmSessionPickles.clear();
    FOlmSessions.clear();
}

void MatrixOlmCrypto::clearOlmSessions()
{
    FOlmSessions.clear();
    FOlmSessionPickles.clear();
}

void MatrixOlmCrypto::clearOlmSession(const QString &userId, const QString &deviceId)
{
    const QString sessionKey = userId + QLatin1Char(':') + deviceId;
    FOlmSessions.remove(sessionKey);
    FOlmSessionPickles.remove(sessionKey);
}

void MatrixOlmCrypto::clearMegolmSession(const QString &sessionId)
{
    if (!sessionId.isEmpty())
        FMegolmSessions.remove(sessionId);
}

bool MatrixOlmCrypto::createAccount()
{
#if HAVE_OLM
    const size_t accountSize = olm_account_size();
    char *account = new char[accountSize];
    OlmAccount *accountObject = olm_account(account);
    if (!accountObject) {
        delete [] account;
        return false;
    }
    QByteArray random(static_cast<int>(olm_create_account_random_length(accountObject)), Qt::Uninitialized);
    for (char &byte : random)
        byte = static_cast<char>(QRandomGenerator::system()->generate() & 0xff);
    if (olm_create_account(accountObject, random.data(), static_cast<size_t>(random.size())) == olm_error()) {
        delete [] account;
        return false;
    }
    FAccount = account;
    return true;
#else
    return false;
#endif
}

bool MatrixOlmCrypto::unpickleAccount(const QByteArray &pickle, const QByteArray &pickleKey)
{
#if HAVE_OLM
    if (pickle.isEmpty() || pickleKey.isEmpty())
        return false;
    const size_t accountSize = olm_account_size();
    char *account = new char[accountSize];
    OlmAccount *accountObject = olm_account(account);
    if (!accountObject ||
        olm_unpickle_account(accountObject, pickleKey.constData(), static_cast<size_t>(pickleKey.size()),
                             const_cast<char *>(pickle.constData()), static_cast<size_t>(pickle.size())) == olm_error()) {
        delete [] account;
        return false;
    }
    FAccount = account;
    FPickleKey = pickleKey;
    return true;
#else
    Q_UNUSED(pickle);
    Q_UNUSED(pickleKey);
    return false;
#endif
}

bool MatrixOlmCrypto::pickleAccount(MatrixDatabase &database)
{
#if HAVE_OLM
    QByteArray pickle;
    if (!pickleAccountData(pickle))
        return false;
    return database.saveOlmAccount(FUserId, FDeviceId, FPickleKey, pickle);
#else
    Q_UNUSED(database);
    return false;
#endif
}

bool MatrixOlmCrypto::pickleAccountData(QByteArray &pickle) const
{
#if HAVE_OLM
    if (!FAccount)
        return false;
    OlmAccount *account = static_cast<OlmAccount *>(FAccount);
    const size_t pickleSize = olm_pickle_account_length(account);
    pickle.resize(static_cast<int>(pickleSize));
    return olm_pickle_account(account, FPickleKey.constData(), static_cast<size_t>(FPickleKey.size()),
                               pickle.data(), pickleSize) != olm_error();
#else
    Q_UNUSED(pickle);
    return false;
#endif
}

bool MatrixOlmCrypto::initializeFromStoredAccount(const QString &userId, const QString &deviceId,
                                                  const QByteArray &pickleKey,
                                                  const QByteArray &pickle)
{
    clear();
    FUserId = userId;
    FDeviceId = deviceId;
    return unpickleAccount(pickle, pickleKey);
}

bool MatrixOlmCrypto::initializeNewAccount(const QString &userId, const QString &deviceId,
                                           QByteArray &pickleKey, QByteArray &pickle)
{
    clear();
    FUserId = userId;
    FDeviceId = deviceId;
#if HAVE_OLM
    FPickleKey.resize(32);
    QRandomGenerator::system()->generate(reinterpret_cast<quint32 *>(FPickleKey.data()),
                                         reinterpret_cast<quint32 *>(FPickleKey.data() + FPickleKey.size()));
#else
    return false;
#endif
    if (!createAccount() || !pickleAccountData(pickle))
        return false;
    pickleKey = FPickleKey;
    return true;
}

bool MatrixOlmCrypto::initialize(MatrixDatabase &database, const QString &userId,
                                 const QString &deviceId)
{
    QByteArray pickle;
    QByteArray pickleKey;
    if (database.loadOlmAccount(userId, deviceId, pickleKey, pickle) &&
        initializeFromStoredAccount(userId, deviceId, pickleKey, pickle))
        return true;
    if (!initializeNewAccount(userId, deviceId, pickleKey, pickle))
        return false;
    return database.saveOlmAccount(userId, deviceId, pickleKey, pickle);
}

QStringList MatrixOlmCrypto::oneTimeKeyIds() const
{
#if HAVE_OLM
    if (!FAccount)
        return {};
    OlmAccount *account = static_cast<OlmAccount *>(FAccount);
    const size_t length = olm_account_one_time_keys_length(account);
    QByteArray serialized(static_cast<int>(length), Qt::Uninitialized);
    if (olm_account_one_time_keys(account, serialized.data(), length) == olm_error())
        return {};
    const QJsonObject root = QJsonDocument::fromJson(serialized).object();
    QStringList ids;
    for (auto algorithm = root.constBegin(); algorithm != root.constEnd(); ++algorithm) {
        const QJsonObject algorithmKeys = algorithm.value().toObject();
        for (auto key = algorithmKeys.constBegin(); key != algorithmKeys.constEnd(); ++key)
            ids.append(QStringLiteral("%1:%2").arg(algorithm.key(), key.key()));
    }
    return ids;
#else
    return {};
#endif
}

QStringList MatrixOlmCrypto::fallbackKeyIds() const
{
#if HAVE_OLM
    if (!FAccount)
        return {};
    OlmAccount *account = static_cast<OlmAccount *>(FAccount);
    const size_t length = olm_account_fallback_key_length(account);
    QByteArray serialized(static_cast<int>(length), Qt::Uninitialized);
    if (length == 0 || olm_account_fallback_key(account, serialized.data(), length) == olm_error())
        return {};
    const QJsonObject keys = QJsonDocument::fromJson(serialized).object()
        .value(QStringLiteral("curve25519")).toObject();
    QStringList ids;
    for (auto it = keys.constBegin(); it != keys.constEnd(); ++it)
        ids.append(QStringLiteral("signed_curve25519:%1").arg(it.key()));
    return ids;
#else
    return {};
#endif
}

QByteArray MatrixOlmCrypto::identityKeysJson() const
{
#if HAVE_OLM
    if (!FAccount)
        return QByteArray();
    OlmAccount *account = static_cast<OlmAccount *>(FAccount);
    const size_t length = olm_account_identity_keys_length(account);
    QByteArray keys(static_cast<int>(length), Qt::Uninitialized);
    if (olm_account_identity_keys(account, keys.data(), length) == olm_error())
        return QByteArray();
    return keys;
#else
    return QByteArray();
#endif
}

QByteArray MatrixOlmCrypto::canonicalJson(const QJsonObject &object) const
{
    return canonicalJsonValue(object);
}

QByteArray MatrixOlmCrypto::signCanonicalJson(const QByteArray &canonicalJson) const
{
#if HAVE_OLM
    if (!FAccount || canonicalJson.isEmpty())
        return QByteArray();
    OlmAccount *account = static_cast<OlmAccount *>(FAccount);
    const size_t signatureLength = olm_account_signature_length(account);
    QByteArray signature(static_cast<int>(signatureLength), Qt::Uninitialized);
    if (olm_account_sign(account, reinterpret_cast<const uint8_t *>(canonicalJson.constData()),
                         static_cast<size_t>(canonicalJson.size()),
                         reinterpret_cast<uint8_t *>(signature.data()), signatureLength) == olm_error())
        return QByteArray();
    return signature;
#else
    Q_UNUSED(canonicalJson);
    return QByteArray();
#endif
}

QByteArray MatrixOlmCrypto::signWithPkSeed(const QByteArray &seed,
                                           const QByteArray &canonicalJson,
                                           QByteArray &publicKey) const
{
#if HAVE_OLM
    if (seed.isEmpty() || canonicalJson.isEmpty())
        return QByteArray();
    const QByteArray rawSeed = QByteArray::fromBase64(seed);
    if (rawSeed.size() != static_cast<int>(olm_pk_signing_seed_length()))
        return QByteArray();
    QByteArray memory(static_cast<int>(olm_pk_signing_size()), Qt::Uninitialized);
    OlmPkSigning *signing = olm_pk_signing(memory.data());
    publicKey.resize(static_cast<int>(olm_pk_signing_public_key_length()));
    if (!signing || olm_pk_signing_key_from_seed(
            signing, publicKey.data(), publicKey.size(), rawSeed.constData(), rawSeed.size()) == olm_error())
        return QByteArray();
    QByteArray signature(static_cast<int>(olm_pk_signature_length()), Qt::Uninitialized);
    if (olm_pk_sign(signing, reinterpret_cast<const uint8_t *>(canonicalJson.constData()), canonicalJson.size(),
                    reinterpret_cast<uint8_t *>(signature.data()), signature.size()) == olm_error())
        return QByteArray();
    return signature.toBase64(QByteArray::Base64Encoding);
#else
    Q_UNUSED(seed); Q_UNUSED(canonicalJson); Q_UNUSED(publicKey);
    return QByteArray();
#endif
}

bool MatrixOlmCrypto::verifyDeviceKeys(const QString &userId, const QString &deviceId,
                                       const QJsonObject &deviceKeys) const
{
#if HAVE_OLM
    if (userId.isEmpty() || deviceId.isEmpty() || deviceKeys.value(QStringLiteral("user_id")).toString() != userId ||
        deviceKeys.value(QStringLiteral("device_id")).toString() != deviceId)
        return false;
    const QJsonObject keys = deviceKeys.value(QStringLiteral("keys")).toObject();
    const QString signingKey = keys.value(QStringLiteral("ed25519:%1").arg(deviceId)).toString();
    const QString signature = deviceKeys.value(QStringLiteral("signatures")).toObject()
        .value(userId).toObject().value(QStringLiteral("ed25519:%1").arg(deviceId)).toString();
    if (signingKey.isEmpty() || signature.isEmpty())
        return false;
    QJsonObject signedObject = deviceKeys;
    signedObject.remove(QStringLiteral("signatures"));
    signedObject.remove(QStringLiteral("unsigned"));
    const QByteArray message = canonicalJsonValue(signedObject);
    QByteArray utilityMemory(static_cast<int>(olm_utility_size()), Qt::Uninitialized);
    OlmUtility *utility = olm_utility(utilityMemory.data());
    const QByteArray encodedKey = signingKey.toLatin1();
    QByteArray encodedSignature = signature.toLatin1();
    return utility && olm_ed25519_verify(utility, encodedKey.constData(), encodedKey.size(),
        message.constData(), message.size(), encodedSignature.data(), encodedSignature.size()) != olm_error();
#else
    Q_UNUSED(userId); Q_UNUSED(deviceId); Q_UNUSED(deviceKeys);
    return false;
#endif
}

bool MatrixOlmCrypto::verifySignedOneTimeKey(const QString &userId, const QString &deviceId,
                                             const QString &signingKey,
                                             const QJsonObject &oneTimeKey) const
{
#if HAVE_OLM
    if (userId.isEmpty() || deviceId.isEmpty() || signingKey.isEmpty() || oneTimeKey.isEmpty())
        return false;
    const QString signature = oneTimeKey.value(QStringLiteral("signatures")).toObject()
        .value(userId).toObject().value(QStringLiteral("ed25519:%1").arg(deviceId)).toString();
    if (signature.isEmpty())
        return false;
    QJsonObject signedObject = oneTimeKey;
    signedObject.remove(QStringLiteral("signatures"));
    const QByteArray message = canonicalJsonValue(signedObject);
    QByteArray utilityMemory(static_cast<int>(olm_utility_size()), Qt::Uninitialized);
    OlmUtility *utility = olm_utility(utilityMemory.data());
    QByteArray encodedKey = signingKey.toLatin1();
    QByteArray encodedSignature = signature.toLatin1();
    return utility && olm_ed25519_verify(utility, encodedKey.constData(), encodedKey.size(),
        message.constData(), message.size(), encodedSignature.data(), encodedSignature.size()) != olm_error();
#else
    Q_UNUSED(userId); Q_UNUSED(deviceId); Q_UNUSED(signingKey); Q_UNUSED(oneTimeKey);
    return false;
#endif
}

bool MatrixOlmCrypto::verifyCrossSigningKey(const QString &userId,
                                            const QJsonObject &keyObject,
                                            const QString &signingKeyId,
                                            const QString &signingKey) const
{
#if HAVE_OLM
    if (userId.isEmpty() || signingKeyId.isEmpty() || signingKey.isEmpty() ||
        keyObject.value(QStringLiteral("user_id")).toString() != userId)
        return false;
    const QJsonObject keys = keyObject.value(QStringLiteral("keys")).toObject();
    if (keys.size() != 1)
        return false;
    const QString signature = keyObject.value(QStringLiteral("signatures")).toObject()
        .value(userId).toObject().value(signingKeyId).toString();
    if (signature.isEmpty())
        return false;
    QJsonObject signedObject = keyObject;
    signedObject.remove(QStringLiteral("signatures"));
    signedObject.remove(QStringLiteral("unsigned"));
    const QByteArray message = canonicalJsonValue(signedObject);
    QByteArray utilityMemory(static_cast<int>(olm_utility_size()), Qt::Uninitialized);
    OlmUtility *utility = olm_utility(utilityMemory.data());
    QByteArray encodedKey = signingKey.toLatin1();
    QByteArray encodedSignature = signature.toLatin1();
    return utility && olm_ed25519_verify(utility, encodedKey.constData(), encodedKey.size(),
        message.constData(), message.size(), encodedSignature.data(), encodedSignature.size()) != olm_error();
#else
    Q_UNUSED(userId); Q_UNUSED(keyObject); Q_UNUSED(signingKeyId); Q_UNUSED(signingKey);
    return false;
#endif
}

bool MatrixOlmCrypto::verifyDeviceCrossSignature(const QString &userId,
                                                 const QJsonObject &deviceKeys,
                                                 const QString &signingKeyId,
                                                 const QString &signingKey) const
{
#if HAVE_OLM
    if (userId.isEmpty() || signingKeyId.isEmpty() || signingKey.isEmpty() ||
        deviceKeys.value(QStringLiteral("user_id")).toString() != userId)
        return false;
    const QString signature = deviceKeys.value(QStringLiteral("signatures")).toObject()
        .value(userId).toObject().value(signingKeyId).toString();
    if (signature.isEmpty())
        return false;
    QJsonObject signedObject = deviceKeys;
    signedObject.remove(QStringLiteral("signatures"));
    signedObject.remove(QStringLiteral("unsigned"));
    const QByteArray message = canonicalJsonValue(signedObject);
    QByteArray utilityMemory(static_cast<int>(olm_utility_size()), Qt::Uninitialized);
    OlmUtility *utility = olm_utility(utilityMemory.data());
    QByteArray encodedKey = signingKey.toLatin1();
    QByteArray encodedSignature = signature.toLatin1();
    return utility && olm_ed25519_verify(utility, encodedKey.constData(), encodedKey.size(),
        message.constData(), message.size(), encodedSignature.data(), encodedSignature.size()) != olm_error();
#else
    Q_UNUSED(userId); Q_UNUSED(deviceKeys); Q_UNUSED(signingKeyId); Q_UNUSED(signingKey);
    return false;
#endif
}

QByteArray MatrixOlmCrypto::prepareKeysUpload(const QString &userId, const QString &deviceId,
                                              int count, bool replaceFallback)
{
#if HAVE_OLM
    if (!FAccount || count < 0)
        return QByteArray();
    OlmAccount *account = static_cast<OlmAccount *>(FAccount);
    if (count > 0) {
        const size_t randomLength = olm_account_generate_one_time_keys_random_length(account, count);
        QByteArray random(static_cast<int>(randomLength), Qt::Uninitialized);
        for (char &byte : random)
            byte = static_cast<char>(QRandomGenerator::system()->generate() & 0xff);
        if (olm_account_generate_one_time_keys(account, count, random.data(), randomLength) == olm_error())
            return QByteArray();
    }
    bool hasFallbackKey = false;
    const size_t fallbackJsonLength = olm_account_fallback_key_length(account);
    if (fallbackJsonLength > 0) {
        QByteArray fallbackJson(static_cast<int>(fallbackJsonLength), Qt::Uninitialized);
        if (olm_account_fallback_key(account, fallbackJson.data(), fallbackJsonLength) == olm_error())
            return QByteArray();
        const QJsonObject fallbackObject = QJsonDocument::fromJson(fallbackJson).object();
        hasFallbackKey = !fallbackObject.value(QStringLiteral("curve25519")).toObject().isEmpty();
    }
    bool hasUnpublishedFallbackKey = false;
    const size_t unpublishedFallbackJsonLength = olm_account_unpublished_fallback_key_length(account);
    if (unpublishedFallbackJsonLength > 0) {
        QByteArray unpublishedFallbackJson(static_cast<int>(unpublishedFallbackJsonLength), Qt::Uninitialized);
        if (olm_account_unpublished_fallback_key(account, unpublishedFallbackJson.data(),
                unpublishedFallbackJsonLength) == olm_error())
            return QByteArray();
        const QJsonObject unpublishedFallback = QJsonDocument::fromJson(unpublishedFallbackJson).object();
        hasUnpublishedFallbackKey = !unpublishedFallback.value(QStringLiteral("curve25519"))
            .toObject().isEmpty();
    }
    if (!hasFallbackKey || (replaceFallback && !hasUnpublishedFallbackKey)) {
        const size_t fallbackRandomLength = olm_account_generate_fallback_key_random_length(account);
        QByteArray fallbackRandom(static_cast<int>(fallbackRandomLength), Qt::Uninitialized);
        for (char &byte : fallbackRandom)
            byte = static_cast<char>(QRandomGenerator::system()->generate() & 0xff);
        if (olm_account_generate_fallback_key(account, fallbackRandom.data(), fallbackRandomLength) == olm_error())
            return QByteArray();
    }

    QJsonParseError identityError;
    const QJsonDocument identity = QJsonDocument::fromJson(identityKeysJson(), &identityError);
    const size_t oneTimeLength = olm_account_one_time_keys_length(account);
    QByteArray oneTime(static_cast<int>(oneTimeLength), Qt::Uninitialized);
    if (olm_account_one_time_keys(account, oneTime.data(), oneTimeLength) == olm_error())
        return QByteArray();
    QJsonParseError oneTimeError;
    const QJsonDocument oneTimeKeys = QJsonDocument::fromJson(oneTime, &oneTimeError);
    if (identityError.error != QJsonParseError::NoError || oneTimeError.error != QJsonParseError::NoError)
        return QByteArray();

    QJsonObject deviceKeys;
    deviceKeys.insert(QStringLiteral("user_id"), userId);
    deviceKeys.insert(QStringLiteral("device_id"), deviceId);
    deviceKeys.insert(QStringLiteral("algorithms"), QJsonArray{
        QStringLiteral("m.olm.v1.curve25519-aes-sha2"),
        QStringLiteral("m.megolm.v1.aes-sha2")});
    const QJsonObject identityKeys = identity.object();
    QJsonObject matrixIdentityKeys;
    for (auto it = identityKeys.constBegin(); it != identityKeys.constEnd(); ++it)
        matrixIdentityKeys.insert(QStringLiteral("%1:%2").arg(it.key(), deviceId), it.value());
    deviceKeys.insert(QStringLiteral("keys"), matrixIdentityKeys);
	const QByteArray signedJson = canonicalJsonValue(deviceKeys);
	const size_t signatureLength = olm_account_signature_length(account);
	QByteArray signature(static_cast<int>(signatureLength), Qt::Uninitialized);
	if (olm_account_sign(account, reinterpret_cast<const uint8_t *>(signedJson.constData()),
		static_cast<size_t>(signedJson.size()), reinterpret_cast<uint8_t *>(signature.data()),
		signatureLength) == olm_error())
		return QByteArray();
	QJsonObject deviceSignatures;
	deviceSignatures.insert(QStringLiteral("ed25519:%1").arg(deviceId),
		QString::fromLatin1(signature));
	QJsonObject signaturesForUser;
	signaturesForUser.insert(userId, deviceSignatures);
	deviceKeys.insert(QStringLiteral("signatures"), signaturesForUser);

    QJsonObject matrixOneTimeKeys;
    const QJsonObject rawOneTimeKeys = oneTimeKeys.object();
    const size_t oneTimeSignatureLength = olm_account_signature_length(account);
    for (auto algorithmIt = rawOneTimeKeys.constBegin(); algorithmIt != rawOneTimeKeys.constEnd(); ++algorithmIt) {
        const QJsonObject algorithmKeys = algorithmIt.value().toObject();
        for (auto keyIt = algorithmKeys.constBegin(); keyIt != algorithmKeys.constEnd(); ++keyIt) {
            QJsonObject signedKey;
            signedKey.insert(QStringLiteral("key"), keyIt.value());
            const QByteArray signedKeyJson = canonicalJsonValue(signedKey);
            QByteArray oneTimeSignature(static_cast<int>(oneTimeSignatureLength), Qt::Uninitialized);
            if (olm_account_sign(account, reinterpret_cast<const uint8_t *>(signedKeyJson.constData()),
                    static_cast<size_t>(signedKeyJson.size()),
                    reinterpret_cast<uint8_t *>(oneTimeSignature.data()), oneTimeSignatureLength) == olm_error())
                return QByteArray();
            QJsonObject keySignatures;
            keySignatures.insert(QStringLiteral("ed25519:%1").arg(deviceId),
                QString::fromLatin1(oneTimeSignature));
            QJsonObject signaturesForUser;
            signaturesForUser.insert(userId, keySignatures);
            signedKey.insert(QStringLiteral("signatures"), signaturesForUser);
            matrixOneTimeKeys.insert(QStringLiteral("signed_%1:%2").arg(algorithmIt.key(), keyIt.key()), signedKey);
        }
    }

    QJsonObject fallbackKeys;
    const size_t fallbackLength = olm_account_unpublished_fallback_key_length(account);
    if (fallbackLength > 0) {
        QByteArray fallback(static_cast<int>(fallbackLength), Qt::Uninitialized);
        if (olm_account_unpublished_fallback_key(account, fallback.data(), fallbackLength) == olm_error())
            return QByteArray();
        const QJsonObject rawFallback = QJsonDocument::fromJson(fallback).object();
        for (auto algorithmIt = rawFallback.constBegin(); algorithmIt != rawFallback.constEnd(); ++algorithmIt) {
            const QJsonObject algorithmKeys = algorithmIt.value().toObject();
            for (auto keyIt = algorithmKeys.constBegin(); keyIt != algorithmKeys.constEnd(); ++keyIt) {
                QJsonObject signedKey;
                signedKey.insert(QStringLiteral("key"), keyIt.value());
                const QByteArray signedKeyJson = canonicalJsonValue(signedKey);
                QByteArray fallbackSignature(static_cast<int>(oneTimeSignatureLength), Qt::Uninitialized);
                if (olm_account_sign(account, reinterpret_cast<const uint8_t *>(signedKeyJson.constData()),
                        static_cast<size_t>(signedKeyJson.size()),
                        reinterpret_cast<uint8_t *>(fallbackSignature.data()), oneTimeSignatureLength) == olm_error())
                    return QByteArray();
                signedKey.insert(QStringLiteral("signatures"),
                    QJsonObject{{userId, QJsonObject{{QStringLiteral("ed25519:%1").arg(deviceId),
                        QString::fromLatin1(fallbackSignature)}}}});
                fallbackKeys.insert(QStringLiteral("signed_%1:%2").arg(algorithmIt.key(), keyIt.key()), signedKey);
            }
        }
    }

    QJsonObject upload;
    upload.insert(QStringLiteral("device_keys"), deviceKeys);
    upload.insert(QStringLiteral("one_time_keys"), matrixOneTimeKeys);
    if (!fallbackKeys.isEmpty())
        upload.insert(QStringLiteral("fallback_keys"), fallbackKeys);
    return QJsonDocument(upload).toJson(QJsonDocument::Compact);
#else
    Q_UNUSED(userId);
    Q_UNUSED(deviceId);
    Q_UNUSED(count);
    return QByteArray();
#endif
}

void MatrixOlmCrypto::markKeysAsPublished()
{
#if HAVE_OLM
    if (FAccount)
        olm_account_mark_keys_as_published(static_cast<OlmAccount *>(FAccount));
#endif
}

bool MatrixOlmCrypto::persistAccount(MatrixDatabase &database)
{
    return pickleAccount(database);
}

bool MatrixOlmCrypto::importMegolmSession(MatrixDatabase &database, const QString &sessionId,
                                          const QString &roomId, const QString &senderKey,
                                          const QByteArray &sessionKey)
{
#if HAVE_OLM
    if (sessionId.isEmpty() || roomId.isEmpty() || sessionKey.isEmpty())
        return false;
    QSharedPointer<QByteArray> memory = QSharedPointer<QByteArray>::create(
        static_cast<int>(olm_inbound_group_session_size()), Qt::Uninitialized);
    OlmInboundGroupSession *session = olm_inbound_group_session(memory->data());
    if (!session || olm_init_inbound_group_session(
            session, reinterpret_cast<const uint8_t *>(sessionKey.constData()),
            static_cast<size_t>(sessionKey.size())) == olm_error()) {
        qWarning() << "[Matrix-E2EE] local inbound Megolm import failed:"
                   << "libolm_error:" << (session ? olm_inbound_group_session_last_error(session) : "no_session")
                   << "sessionIdLength:" << sessionId.size()
                   << "sessionIdPlaceholder:" << (sessionId == QString(43, QLatin1Char('A')))
                   << "roomId:" << roomId
                   << "senderKeyPresent:" << !senderKey.isEmpty()
                   << "keyBytes:" << sessionKey.size()
                   << "keyChars:" << sessionKey.size();
        return false;
    }
    const QString actualSessionId = inboundMegolmSessionId(session);
    if (actualSessionId != sessionId) {
        qWarning() << "[Matrix-E2EE] rejected outbound Megolm session ID mismatch:"
                   << "expected:" << sessionId << "actual:" << actualSessionId;
        return false;
    }
    const size_t pickleLength = olm_pickle_inbound_group_session_length(session);
    QByteArray pickle(static_cast<int>(pickleLength), Qt::Uninitialized);
    static const QByteArray pickleKeyLiteral("matrix-megolm-session");
    if (olm_pickle_inbound_group_session(
            session, pickleKeyLiteral.constData(), static_cast<size_t>(pickleKeyLiteral.size()),
            pickle.data(), pickleLength) == olm_error())
        return false;
    if (!database.saveMegolmSession(sessionId, roomId, senderKey, pickle)) {
        qWarning() << "[Matrix-E2EE] local inbound Megolm SQLite save failed:"
                   << sessionId << roomId;
        return false;
    }
    FMegolmSessions.insert(sessionId, memory);
    return true;
#else
    Q_UNUSED(database);
    Q_UNUSED(sessionId);
    Q_UNUSED(roomId);
    Q_UNUSED(senderKey);
    Q_UNUSED(sessionKey);
    return false;
#endif
}

bool MatrixOlmCrypto::prepareMegolmSession(const QString &sessionId,
                                           const QByteArray &sessionKey,
                                           QByteArray &pickle)
{
#if HAVE_OLM
    if (sessionId.isEmpty() || sessionKey.isEmpty())
        return false;
    QSharedPointer<QByteArray> memory = QSharedPointer<QByteArray>::create(
        static_cast<int>(olm_inbound_group_session_size()), Qt::Uninitialized);
    OlmInboundGroupSession *session = olm_inbound_group_session(memory->data());
    if (!session || olm_init_inbound_group_session(
            session, reinterpret_cast<const uint8_t *>(sessionKey.constData()),
            static_cast<size_t>(sessionKey.size())) == olm_error()) {
        qWarning() << "[Matrix-E2EE] Megolm export session init failed:" << sessionId
            << "error:" << (session ? olm_inbound_group_session_last_error(session) : "no_session")
            << "session_key_bytes:" << sessionKey.size();
        return false;
    }
    const QString actualSessionId = inboundMegolmSessionId(session);
    if (actualSessionId != sessionId) {
        qWarning() << "[Matrix-E2EE] rejected direct Megolm session ID mismatch:"
            << "expected:" << sessionId << "actual:" << actualSessionId;
        return false;
    }
    const size_t pickleLength = olm_pickle_inbound_group_session_length(session);
    pickle.resize(static_cast<int>(pickleLength));
    static const QByteArray pickleKeyLiteral("matrix-megolm-session");
    if (olm_pickle_inbound_group_session(
            session, pickleKeyLiteral.constData(), static_cast<size_t>(pickleKeyLiteral.size()),
            pickle.data(), pickleLength) == olm_error())
        return false;
    FMegolmSessions.insert(sessionId, memory);
    return true;
#else
    Q_UNUSED(sessionId);
    Q_UNUSED(sessionKey);
    Q_UNUSED(pickle);
    return false;
#endif
}

bool MatrixOlmCrypto::prepareExportedMegolmSession(const QString &sessionId,
                                                    const QByteArray &sessionKey,
                                                    QByteArray &pickle)
{
#if HAVE_OLM
    if (sessionId.isEmpty() || sessionKey.isEmpty())
        return false;
    QSharedPointer<QByteArray> memory = QSharedPointer<QByteArray>::create(
        static_cast<int>(olm_inbound_group_session_size()), Qt::Uninitialized);
    OlmInboundGroupSession *session = olm_inbound_group_session(memory->data());
    if (!session || olm_import_inbound_group_session(
            session, reinterpret_cast<const uint8_t *>(sessionKey.constData()),
            static_cast<size_t>(sessionKey.size())) == olm_error())
        return false;
    const QString actualSessionId = inboundMegolmSessionId(session);
    if (actualSessionId != sessionId) {
        qWarning() << "[Matrix-E2EE] rejected forwarded Megolm session ID mismatch:"
            << "expected:" << sessionId << "actual:" << actualSessionId;
        return false;
    }
    const size_t pickleLength = olm_pickle_inbound_group_session_length(session);
    pickle.resize(static_cast<int>(pickleLength));
    static const QByteArray pickleKeyLiteral("matrix-megolm-session");
    if (olm_pickle_inbound_group_session(session, pickleKeyLiteral.constData(),
            static_cast<size_t>(pickleKeyLiteral.size()), pickle.data(), pickleLength) == olm_error())
        return false;
    FMegolmSessions.insert(sessionId, memory);
    return true;
#else
    Q_UNUSED(sessionId); Q_UNUSED(sessionKey); Q_UNUSED(pickle);
    return false;
#endif
}

QByteArray MatrixOlmCrypto::decryptMegolm(MatrixDatabase &database, const QString &sessionId,
                                          const QByteArray &ciphertext)
{
    return decryptMegolmDetailed(database, sessionId, ciphertext).plaintext;
}

MatrixMegolmDecryptResult MatrixOlmCrypto::decryptMegolmDetailedWithPickle(
    const QString &sessionId, const QByteArray &ciphertext, const QByteArray &pickle)
{
    MatrixMegolmDecryptResult result;
#if HAVE_OLM
    if (sessionId.isEmpty() || ciphertext.isEmpty()) {
        result.status = QStringLiteral("invalid");
        result.error = QStringLiteral("missing_session_or_ciphertext");
        return result;
    }
    if (!pickle.isEmpty())
        FMegolmSessions.remove(sessionId);
    QSharedPointer<QByteArray> explicitMemory;
    OlmInboundGroupSession *session = nullptr;
    if (!pickle.isEmpty()) {
        explicitMemory = QSharedPointer<QByteArray>::create(
            static_cast<int>(olm_inbound_group_session_size()), Qt::Uninitialized);
        session = olm_inbound_group_session(explicitMemory->data());
        static const QByteArray pickleKeyLiteral("matrix-megolm-session");
        QByteArray mutablePickle = pickle;
        if (!session || olm_unpickle_inbound_group_session(
                session, pickleKeyLiteral.constData(), static_cast<size_t>(pickleKeyLiteral.size()),
                mutablePickle.data(), static_cast<size_t>(mutablePickle.size())) == olm_error()) {
            result.status = QStringLiteral("failed");
            result.error = QStringLiteral("invalid_session_pickle");
            return result;
        }
        FMegolmSessions.insert(sessionId, explicitMemory);
    }
    if (!session) {
        const auto cached = FMegolmSessions.value(sessionId);
        if (cached.isNull()) {
            result.status = QStringLiteral("pending");
            result.error = QStringLiteral("missing_session");
            return result;
        }
        session = reinterpret_cast<OlmInboundGroupSession *>(cached->data());
    }
    QByteArray sizeProbe = ciphertext;
    const size_t maxLength = olm_group_decrypt_max_plaintext_length(
        session, reinterpret_cast<uint8_t *>(sizeProbe.data()), static_cast<size_t>(sizeProbe.size()));
    if (maxLength == olm_error()) {
        result.status = QStringLiteral("failed");
        result.error = QString::fromLatin1(olm_inbound_group_session_last_error(session));
        return result;
    }
    QByteArray message = ciphertext;
    QByteArray plaintext(static_cast<int>(maxLength), Qt::Uninitialized);
    const size_t length = olm_group_decrypt(
        session, reinterpret_cast<uint8_t *>(message.data()), static_cast<size_t>(message.size()),
        reinterpret_cast<uint8_t *>(plaintext.data()), static_cast<size_t>(plaintext.size()),
        &result.messageIndex);
    if (length == olm_error()) {
        result.status = QStringLiteral("failed");
        result.error = QString::fromLatin1(olm_inbound_group_session_last_error(session));
        return result;
    }
    plaintext.resize(static_cast<int>(length));
    result.plaintext = plaintext;
    result.status = QStringLiteral("decrypted");
    return result;
#else
    Q_UNUSED(sessionId);
    Q_UNUSED(ciphertext);
    Q_UNUSED(pickle);
    result.status = QStringLiteral("unsupported");
    result.error = QStringLiteral("libolm_unavailable");
    return result;
#endif
}

MatrixMegolmDecryptResult MatrixOlmCrypto::decryptMegolmDetailed(
    MatrixDatabase &database, const QString &sessionId, const QByteArray &ciphertext)
{
    MatrixMegolmDecryptResult result;
#if HAVE_OLM
    if (sessionId.isEmpty() || ciphertext.isEmpty()) {
        result.status = QStringLiteral("invalid");
        result.error = QStringLiteral("missing_session_or_ciphertext");
        return result;
    }
    if (!FMegolmSessions.contains(sessionId)) {
        QString roomId;
        QString senderKey;
        QByteArray pickle;
        if (!database.loadMegolmSession(sessionId, roomId, senderKey, pickle)) {
            result.status = QStringLiteral("pending");
            result.error = QStringLiteral("missing_session");
            return result;
        }
        QSharedPointer<QByteArray> memory = QSharedPointer<QByteArray>::create(
        static_cast<int>(olm_inbound_group_session_size()), Qt::Uninitialized);
        OlmInboundGroupSession *session = olm_inbound_group_session(memory->data());
        static const QByteArray pickleKeyLiteral("matrix-megolm-session");
        QByteArray mutablePickle = pickle;
        if (!session || olm_unpickle_inbound_group_session(
                session, pickleKeyLiteral.constData(), static_cast<size_t>(pickleKeyLiteral.size()),
                mutablePickle.data(), static_cast<size_t>(mutablePickle.size())) == olm_error()) {
            result.status = QStringLiteral("failed");
            result.error = QStringLiteral("invalid_session_pickle");
            return result;
        }
        FMegolmSessions.insert(sessionId, memory);
    }
    OlmInboundGroupSession *session = reinterpret_cast<OlmInboundGroupSession *>(FMegolmSessions[sessionId]->data());
    QByteArray sizeProbe = ciphertext;
    const size_t maxLength = olm_group_decrypt_max_plaintext_length(
        session, reinterpret_cast<uint8_t *>(sizeProbe.data()), static_cast<size_t>(sizeProbe.size()));
    if (maxLength == olm_error()) {
        result.status = QStringLiteral("failed");
        result.error = QString::fromLatin1(olm_inbound_group_session_last_error(session));
        return result;
    }
    QByteArray message = ciphertext;
    QByteArray plaintext(static_cast<int>(maxLength), Qt::Uninitialized);
    const size_t length = olm_group_decrypt(
        session, reinterpret_cast<uint8_t *>(message.data()), static_cast<size_t>(message.size()),
        reinterpret_cast<uint8_t *>(plaintext.data()), static_cast<size_t>(plaintext.size()),
        &result.messageIndex);
    if (length == olm_error()) {
        result.status = QStringLiteral("failed");
        result.error = QString::fromLatin1(olm_inbound_group_session_last_error(session));
        return result;
    }
    plaintext.resize(static_cast<int>(length));
    result.plaintext = plaintext;
    result.status = QStringLiteral("decrypted");
    return result;
#else
    Q_UNUSED(database);
    Q_UNUSED(sessionId);
    Q_UNUSED(ciphertext);
    result.status = QStringLiteral("unsupported");
    result.error = QStringLiteral("libolm_unavailable");
    return result;
#endif
}

bool MatrixOlmCrypto::persistMegolmSession(MatrixDatabase &database, const QString &sessionId,
                                           const QString &roomId, const QString &senderKey)
{
#if HAVE_OLM
    if (sessionId.isEmpty() || roomId.isEmpty() || senderKey.isEmpty() ||
        !FMegolmSessions.contains(sessionId))
        return false;
    OlmInboundGroupSession *session = reinterpret_cast<OlmInboundGroupSession *>(FMegolmSessions[sessionId]->data());
    const size_t pickleLength = olm_pickle_inbound_group_session_length(session);
    QByteArray pickle(static_cast<int>(pickleLength), Qt::Uninitialized);
    static const QByteArray pickleKeyLiteral("matrix-megolm-session");
    if (olm_pickle_inbound_group_session(session, pickleKeyLiteral.constData(),
            static_cast<size_t>(pickleKeyLiteral.size()), pickle.data(), pickleLength) == olm_error())
        return false;
    return database.saveMegolmSession(sessionId, roomId, senderKey, pickle);
#else
    Q_UNUSED(database); Q_UNUSED(sessionId); Q_UNUSED(roomId); Q_UNUSED(senderKey);
    return false;
#endif
}

QByteArray MatrixOlmCrypto::exportMegolmSession(MatrixDatabase &database,
                                                const QString &sessionId,
                                                uint32_t messageIndex)
{
#if HAVE_OLM
    QString roomId;
    QString senderKey;
    QByteArray pickle;
    if (!database.loadMegolmSession(sessionId, roomId, senderKey, pickle))
        return QByteArray();
    return exportMegolmSessionWithPickle(sessionId, pickle, messageIndex);
#else
    Q_UNUSED(database); Q_UNUSED(sessionId); Q_UNUSED(messageIndex);
    return QByteArray();
#endif
}

QByteArray MatrixOlmCrypto::exportMegolmSessionWithPickle(const QString &sessionId,
                                                          const QByteArray &pickle,
                                                          uint32_t messageIndex)
{
#if HAVE_OLM
    if (sessionId.isEmpty())
        return QByteArray();
    if (!FMegolmSessions.contains(sessionId)) {
        QSharedPointer<QByteArray> memory = QSharedPointer<QByteArray>::create(
        static_cast<int>(olm_inbound_group_session_size()), Qt::Uninitialized);
        OlmInboundGroupSession *session = olm_inbound_group_session(memory->data());
        static const QByteArray pickleKeyLiteral("matrix-megolm-session");
        QByteArray mutablePickle = pickle;
        if (!session || olm_unpickle_inbound_group_session(
                session, pickleKeyLiteral.constData(), static_cast<size_t>(pickleKeyLiteral.size()),
                mutablePickle.data(), static_cast<size_t>(mutablePickle.size())) == olm_error())
            return QByteArray();
        FMegolmSessions.insert(sessionId, memory);
    }
    OlmInboundGroupSession *session = reinterpret_cast<OlmInboundGroupSession *>(FMegolmSessions[sessionId]->data());
    const size_t keyLength = olm_export_inbound_group_session_length(session);
    QByteArray key(static_cast<int>(keyLength), Qt::Uninitialized);
    const size_t length = olm_export_inbound_group_session(
        session, reinterpret_cast<uint8_t *>(key.data()), keyLength, messageIndex);
    if (length == olm_error())
        return QByteArray();
    key.resize(static_cast<int>(length));
    return key;
#else
    Q_UNUSED(sessionId); Q_UNUSED(pickle); Q_UNUSED(messageIndex);
    return QByteArray();
#endif
}

bool MatrixOlmCrypto::createOlmSession(MatrixDatabase &database, const QString &userId,
                                       const QString &deviceId, const QByteArray &identityKey,
                                       const QByteArray &oneTimeKey)
{
    QString sessionId;
    QByteArray pickle;
    if (!createOlmSessionData(userId, deviceId, identityKey, oneTimeKey, sessionId, pickle))
        return false;
    return database.saveOlmSession(userId, deviceId, sessionId, pickle);
}

bool MatrixOlmCrypto::createOlmSessionData(const QString &userId, const QString &deviceId,
                                           const QByteArray &identityKey,
                                           const QByteArray &oneTimeKey,
                                           QString &sessionId, QByteArray &pickle)
{
#if HAVE_OLM
    if (!FAccount || userId.isEmpty() || deviceId.isEmpty() || identityKey.isEmpty() || oneTimeKey.isEmpty())
        return false;
    const QString sessionKey = userId + QLatin1Char(':') + deviceId;
    // Keep initialized Olm storage at its original address (no QByteArray detach/copy).
    const auto memory = QSharedPointer<QByteArray>::create(
        static_cast<int>(olm_session_size()), Qt::Uninitialized);
    OlmSession *session = olm_session(memory->data());
    const size_t randomLength = olm_create_outbound_session_random_length(session);
    QByteArray random(static_cast<int>(randomLength), Qt::Uninitialized);
    for (char &byte : random)
        byte = static_cast<char>(QRandomGenerator::system()->generate() & 0xff);
    if (!session || olm_create_outbound_session(session, static_cast<OlmAccount *>(FAccount),
            identityKey.constData(), static_cast<size_t>(identityKey.size()),
            oneTimeKey.constData(), static_cast<size_t>(oneTimeKey.size()),
            random.data(), random.size()) == olm_error())
        return false;
    const size_t pickleLength = olm_pickle_session_length(session);
    pickle.resize(static_cast<int>(pickleLength));
    const size_t sessionIdLength = olm_session_id_length(session);
    QByteArray sessionIdBytes(static_cast<int>(sessionIdLength), Qt::Uninitialized);
    if (olm_session_id(session, sessionIdBytes.data(), sessionIdLength) == olm_error() ||
        olm_pickle_session(session, FPickleKey.constData(), static_cast<size_t>(FPickleKey.size()),
                           pickle.data(), pickleLength) == olm_error())
        return false;
    sessionId = QString::fromUtf8(sessionIdBytes);
    FOlmSessionPickles.insert(sessionKey, pickle);
    FOlmSessions.insert(sessionKey, memory);
    return true;
#else
    Q_UNUSED(userId); Q_UNUSED(deviceId); Q_UNUSED(identityKey); Q_UNUSED(oneTimeKey);
    Q_UNUSED(sessionId); Q_UNUSED(pickle);
    return false;
#endif
}

QByteArray MatrixOlmCrypto::encryptOlm(MatrixDatabase &database, const QString &userId,
                                      const QString &deviceId, const QByteArray &plaintext,
                                      int &messageType)
{
#if HAVE_OLM
    QString storedSessionId;
    QByteArray storedPickle;
    if (!database.loadOlmSession(userId, deviceId, storedSessionId, storedPickle))
        return QByteArray();
    if (olmSessionIdWithPickle(userId, deviceId, storedSessionId, storedPickle).isEmpty())
        return QByteArray();
    QString sessionId;
    QByteArray pickle;
    QByteArray message = encryptOlmData(userId, deviceId, plaintext, messageType, sessionId, pickle);
    if (message.isEmpty() || !database.saveOlmSession(userId, deviceId, sessionId, pickle))
        return QByteArray();
    return message;
#else
    Q_UNUSED(database); Q_UNUSED(userId); Q_UNUSED(deviceId); Q_UNUSED(plaintext);
    messageType = -1;
    return QByteArray();
#endif
}

QByteArray MatrixOlmCrypto::encryptOlmData(const QString &userId, const QString &deviceId,
                                           const QByteArray &plaintext, int &messageType,
                                           QString &sessionId, QByteArray &pickle)
{
#if HAVE_OLM
    messageType = -1;
    const QString sessionKey = userId + QLatin1Char(':') + deviceId;
    QSharedPointer<QByteArray> memory;
    OlmSession *session = nullptr;
    if (FOlmSessionPickles.contains(sessionKey)) {
        memory = QSharedPointer<QByteArray>::create(static_cast<int>(olm_session_size()), Qt::Uninitialized);
        session = olm_session(memory->data());
        QByteArray mutablePickle = FOlmSessionPickles.value(sessionKey);
        if (!session || olm_unpickle_session(session, FPickleKey.constData(),
                static_cast<size_t>(FPickleKey.size()), mutablePickle.data(),
                static_cast<size_t>(mutablePickle.size())) == olm_error())
            return QByteArray();
    } else {
        if (!FOlmSessions.contains(sessionKey))
            return QByteArray();
        memory = FOlmSessions.value(sessionKey);
        session = reinterpret_cast<OlmSession *>(memory->data());
    }
    const size_t randomLength = olm_encrypt_random_length(session);
    const size_t messageLength = olm_encrypt_message_length(session, plaintext.size());
    QByteArray random(static_cast<int>(randomLength), Qt::Uninitialized);
    QByteArray message(static_cast<int>(messageLength), Qt::Uninitialized);
    for (char &byte : random)
        byte = static_cast<char>(QRandomGenerator::system()->generate() & 0xff);
    messageType = static_cast<int>(olm_encrypt_message_type(session));
    const size_t length = olm_encrypt(session, plaintext.constData(), plaintext.size(),
                                      random.data(), random.size(), message.data(), message.size());
    if (length == olm_error())
        return QByteArray();
    message.resize(static_cast<int>(length));
    const size_t sessionIdLength = olm_session_id_length(session);
    QByteArray sessionIdBytes(static_cast<int>(sessionIdLength), Qt::Uninitialized);
    if (olm_session_id(session, sessionIdBytes.data(), sessionIdLength) == olm_error())
        return QByteArray();
    sessionId = QString::fromUtf8(sessionIdBytes);
    const size_t pickleLength = olm_pickle_session_length(session);
    pickle.resize(static_cast<int>(pickleLength));
    if (olm_pickle_session(session, FPickleKey.constData(), FPickleKey.size(),
            pickle.data(), pickleLength) == olm_error())
        return QByteArray();
    FOlmSessionPickles.insert(sessionKey, pickle);
    FOlmSessions.insert(sessionKey, memory);
    return message;
#else
    Q_UNUSED(userId); Q_UNUSED(deviceId); Q_UNUSED(plaintext);
    Q_UNUSED(sessionId); Q_UNUSED(pickle);
    messageType = -1;
    return QByteArray();
#endif
}

QByteArray MatrixOlmCrypto::decryptOlm(MatrixDatabase &database, const QString &senderUserId,
                                       const QString &senderKey, int messageType,
                                       const QByteArray &ciphertext, QString &sessionId,
                                       const QString &senderDeviceId)
{
#if HAVE_OLM
    if (!FAccount || senderUserId.isEmpty() || senderKey.isEmpty() || ciphertext.isEmpty())
        return QByteArray();
    const QByteArray encodedSenderKey = senderKey.toLatin1();
    const QString sessionKey = senderUserId + QLatin1Char(':') +
        (senderDeviceId.isEmpty() ? senderKey : senderDeviceId);
    QSharedPointer<QByteArray> memory;
    bool loaded = false;
    QString storedSessionId;
    QByteArray storedPickle;
    if (FOlmSessions.contains(sessionKey)) {
        memory = FOlmSessions.value(sessionKey);
        loaded = true;
    } else if (database.loadOlmSession(senderUserId,
                                       senderDeviceId.isEmpty() ? senderKey : senderDeviceId,
                                       storedSessionId, storedPickle)) {
        memory = QSharedPointer<QByteArray>::create(static_cast<int>(olm_session_size()), Qt::Uninitialized);
        OlmSession *storedSession = olm_session(memory->data());
        QByteArray mutablePickle = storedPickle;
        if (!storedSession || olm_unpickle_session(storedSession, FPickleKey.constData(),
                static_cast<size_t>(FPickleKey.size()), mutablePickle.data(),
                static_cast<size_t>(mutablePickle.size())) == olm_error())
            return QByteArray();
        FOlmSessions.insert(sessionKey, memory);
        loaded = true;
    }
    qWarning() << "[Matrix-E2EE] Olm session lookup:" << senderUserId
        << (senderDeviceId.isEmpty() ? senderKey : senderDeviceId)
        << "loaded:" << loaded << "message_type:" << messageType;
    if (loaded && messageType == static_cast<int>(OLM_MESSAGE_TYPE_PRE_KEY)) {
        OlmSession *storedSession = reinterpret_cast<OlmSession *>(FOlmSessions[sessionKey]->data());
        QByteArray matchMessage = ciphertext;
        const size_t matches = storedSession
            ? olm_matches_inbound_session_from(storedSession, encodedSenderKey.constData(),
                static_cast<size_t>(encodedSenderKey.size()), matchMessage.data(),
                static_cast<size_t>(matchMessage.size()))
            : olm_error();
        if (matches != 1) {
            qWarning() << "[Matrix-E2EE] replacing non-matching Olm session for pre-key message:"
                << sessionKey << "matches:" << matches;
            loaded = false;
        }
    }
    if (!loaded && messageType == static_cast<int>(OLM_MESSAGE_TYPE_PRE_KEY) &&
        !senderDeviceId.isEmpty()) {
        QList<QPair<QString, QByteArray>> candidates = database.loadOlmSessions(senderUserId,
            senderDeviceId);
        for (const auto &candidate : candidates) {
            const auto candidateMemory = QSharedPointer<QByteArray>::create(
                static_cast<int>(olm_session_size()), Qt::Uninitialized);
            OlmSession *candidateSession = olm_session(candidateMemory->data());
            QByteArray candidatePickle = candidate.second;
            if (!candidateSession ||
                olm_unpickle_session(candidateSession, FPickleKey.constData(),
                    static_cast<size_t>(FPickleKey.size()), candidatePickle.data(),
                    static_cast<size_t>(candidatePickle.size())) == olm_error())
                continue;
            QByteArray matchMessage = ciphertext;
            if (olm_matches_inbound_session_from(candidateSession, encodedSenderKey.constData(),
                    static_cast<size_t>(encodedSenderKey.size()), matchMessage.data(),
                    static_cast<size_t>(matchMessage.size())) != 1)
                continue;
            FOlmSessions.insert(sessionKey, candidateMemory);
            loaded = true;
            break;
        }
    }
    bool inboundSessionCreated = false;
    if (!loaded) {
        memory = QSharedPointer<QByteArray>::create(static_cast<int>(olm_session_size()), Qt::Uninitialized);
        OlmSession *inbound = olm_session(memory->data());
        QByteArray message = ciphertext;
        const size_t inboundResult = inbound && messageType == static_cast<int>(OLM_MESSAGE_TYPE_PRE_KEY)
            ? olm_create_inbound_session_from(inbound, static_cast<OlmAccount *>(FAccount),
                encodedSenderKey.constData(), static_cast<size_t>(encodedSenderKey.size()),
                message.data(), static_cast<size_t>(message.size())) : olm_error();
        if (inboundResult == olm_error()) {
            qWarning() << "[Matrix-E2EE] libolm inbound session creation failed:"
                << (inbound ? olm_session_last_error(inbound) : "no_session")
                << "message_type:" << messageType
                << "ciphertext_bytes:" << ciphertext.size()
                << "local_unpublished_otk_count:" << oneTimeKeyIds().size()
                << "local_fallback_ids:" << fallbackKeyIds();
            return QByteArray();
        }
        QByteArray matchMessage = ciphertext;
        const size_t matches = olm_matches_inbound_session_from(inbound,
            encodedSenderKey.constData(), static_cast<size_t>(encodedSenderKey.size()),
            matchMessage.data(), static_cast<size_t>(matchMessage.size()));
        if (matches != 1) {
            qWarning() << "[Matrix-E2EE] libolm inbound session match failed:"
                << olm_session_last_error(inbound) << "matches:" << matches;
            return QByteArray();
        }
        FOlmSessions.insert(sessionKey, memory);
        inboundSessionCreated = true;
    }
    auto tryDecrypt = [&](OlmSession *candidate, QByteArray &result) {
        if (!candidate)
            return false;
        QByteArray message = ciphertext;
        const size_t maxLength = olm_decrypt_max_plaintext_length(candidate,
            static_cast<size_t>(messageType), message.data(), static_cast<size_t>(message.size()));
        if (maxLength == olm_error())
            return false;
        result.resize(static_cast<int>(maxLength));
        message = ciphertext;
        const size_t length = olm_decrypt(candidate, static_cast<size_t>(messageType),
            message.data(), static_cast<size_t>(message.size()), result.data(),
            static_cast<size_t>(result.size()));
        if (length == olm_error())
            return false;
        result.resize(static_cast<int>(length));
        return true;
    };
    QByteArray plaintext;
    OlmSession *session = reinterpret_cast<OlmSession *>(FOlmSessions[sessionKey]->data());
    bool decrypted = tryDecrypt(session, plaintext);
    if (!decrypted && !senderDeviceId.isEmpty()) {
        const QList<QPair<QString, QByteArray>> candidates = database.loadOlmSessions(senderUserId,
            senderDeviceId);
        for (const auto &candidate : candidates) {
            const auto candidateMemory = QSharedPointer<QByteArray>::create(
                static_cast<int>(olm_session_size()), Qt::Uninitialized);
            OlmSession *candidateSession = olm_session(candidateMemory->data());
            QByteArray candidatePickle = candidate.second;
            if (!candidateSession ||
                olm_unpickle_session(candidateSession, FPickleKey.constData(),
                    static_cast<size_t>(FPickleKey.size()), candidatePickle.data(),
                    static_cast<size_t>(candidatePickle.size())) == olm_error())
                continue;
            if (messageType == static_cast<int>(OLM_MESSAGE_TYPE_PRE_KEY)) {
                QByteArray matchMessage = ciphertext;
                if (olm_matches_inbound_session_from(candidateSession, encodedSenderKey.constData(),
                        static_cast<size_t>(encodedSenderKey.size()), matchMessage.data(),
                        static_cast<size_t>(matchMessage.size())) != 1)
                    continue;
            }
            if (tryDecrypt(candidateSession, plaintext)) {
                FOlmSessions.insert(sessionKey, candidateMemory);
                session = reinterpret_cast<OlmSession *>(FOlmSessions[sessionKey]->data());
                decrypted = true;
                break;
            }
            qWarning() << "[Matrix-E2EE] Olm decrypt candidate rejected:"
                << "session_id:" << candidate.first
                << "error:" << olm_session_last_error(candidateSession)
                << "message_type:" << messageType;
        }
    }
    if (!decrypted) {
        qWarning() << "[Matrix-E2EE] libolm decrypt failed for all known sessions:"
            << "message_type:" << messageType << "session:" << sessionKey;
        return QByteArray();
    }
    if (inboundSessionCreated &&
        (olm_remove_one_time_keys(static_cast<OlmAccount *>(FAccount),
             session) == olm_error() ||
         !persistAccount(database)))
        return QByteArray();
    const size_t idLength = olm_session_id_length(session);
    QByteArray id(static_cast<int>(idLength), Qt::Uninitialized);
    if (olm_session_id(session, id.data(), idLength) == olm_error())
        return QByteArray();
    sessionId = QString::fromUtf8(id);
    const size_t pickleLength = olm_pickle_session_length(session);
    QByteArray pickle(static_cast<int>(pickleLength), Qt::Uninitialized);
    if (olm_pickle_session(session, FPickleKey.constData(), static_cast<size_t>(FPickleKey.size()),
            pickle.data(), pickleLength) == olm_error() ||
        !database.saveOlmSession(senderUserId,
            senderDeviceId.isEmpty() ? senderKey : senderDeviceId, sessionId, pickle))
        return QByteArray();
    FOlmSessionPickles.insert(sessionKey, pickle);
    return plaintext;
#else
    Q_UNUSED(database); Q_UNUSED(senderUserId); Q_UNUSED(senderKey); Q_UNUSED(senderDeviceId);
    Q_UNUSED(messageType); Q_UNUSED(ciphertext); Q_UNUSED(sessionId);
    return QByteArray();
#endif
}

QString MatrixOlmCrypto::olmSessionId(MatrixDatabase &database, const QString &userId,
                                      const QString &deviceId) const
{
#if HAVE_OLM
    const QString sessionKey = userId + QLatin1Char(':') + deviceId;
    if (FOlmSessions.contains(sessionKey)) {
        const QByteArray &memory = *FOlmSessions.value(sessionKey);
        OlmSession *session = reinterpret_cast<OlmSession *>(const_cast<char *>(memory.constData()));
        const size_t length = olm_session_id_length(session);
        QByteArray id(static_cast<int>(length), Qt::Uninitialized);
        if (olm_session_id(session, id.data(), length) != olm_error())
            return QString::fromUtf8(id);
    }
    QString sessionId;
    QByteArray pickle;
    if (database.loadOlmSession(userId, deviceId, sessionId, pickle))
        return sessionId;
#else
    Q_UNUSED(database); Q_UNUSED(userId); Q_UNUSED(deviceId);
#endif
    return QString();
}

QString MatrixOlmCrypto::olmSessionIdWithPickle(const QString &userId, const QString &deviceId,
                                                const QString &storedSessionId,
                                                const QByteArray &pickle)
{
#if HAVE_OLM
    const QString sessionKey = userId + QLatin1Char(':') + deviceId;
    if (FOlmSessions.contains(sessionKey)) {
        OlmSession *session = reinterpret_cast<OlmSession *>(FOlmSessions[sessionKey]->data());
        const size_t length = olm_session_id_length(session);
        QByteArray id(static_cast<int>(length), Qt::Uninitialized);
        return olm_session_id(session, id.data(), length) == olm_error() ? QString() : QString::fromUtf8(id);
    }
    const auto memory = QSharedPointer<QByteArray>::create(
        static_cast<int>(olm_session_size()), Qt::Uninitialized);
    OlmSession *session = olm_session(memory->data());
    QByteArray mutablePickle = pickle;
    if (!session || olm_unpickle_session(session, FPickleKey.constData(),
            static_cast<size_t>(FPickleKey.size()), mutablePickle.data(), mutablePickle.size()) == olm_error())
        return QString();
    FOlmSessionPickles.insert(sessionKey, pickle);
    FOlmSessions.insert(sessionKey, memory);
    Q_UNUSED(storedSessionId);
    const size_t length = olm_session_id_length(session);
    QByteArray id(static_cast<int>(length), Qt::Uninitialized);
    return olm_session_id(session, id.data(), length) == olm_error() ? QString() : QString::fromUtf8(id);
#else
    Q_UNUSED(userId); Q_UNUSED(deviceId); Q_UNUSED(storedSessionId); Q_UNUSED(pickle);
    return QString();
#endif
}

bool MatrixOlmCrypto::createOutboundMegolmSession(MatrixDatabase &database, const QString &roomId,
                                                  QString &sessionId, QByteArray &sessionKey)
{
    QByteArray pickle;
    if (!createOutboundMegolmSessionData(roomId, sessionId, sessionKey, pickle))
        return false;
    return database.saveOutboundMegolmSession(roomId, sessionId, pickle,
        QDateTime::currentSecsSinceEpoch());
}

bool MatrixOlmCrypto::createOutboundMegolmSessionData(const QString &roomId, QString &sessionId,
                                                     QByteArray &sessionKey, QByteArray &pickle) const
{
#if HAVE_OLM
    if (roomId.isEmpty())
        return false;
    const QSharedPointer<QByteArray> memory = QSharedPointer<QByteArray>::create(
        static_cast<int>(olm_outbound_group_session_size()), Qt::Uninitialized);
    OlmOutboundGroupSession *session = olm_outbound_group_session(memory->data());
    const size_t randomLength = olm_init_outbound_group_session_random_length(session);
    QByteArray random(static_cast<int>(randomLength), Qt::Uninitialized);
    for (char &byte : random)
        byte = static_cast<char>(QRandomGenerator::system()->generate() & 0xff);
    if (!session || olm_init_outbound_group_session(session,
            reinterpret_cast<uint8_t *>(random.data()), random.size()) == olm_error())
        return false;
    const size_t idLength = olm_outbound_group_session_id_length(session);
    QByteArray id(static_cast<int>(idLength), Qt::Uninitialized);
    if (olm_outbound_group_session_id(session, reinterpret_cast<uint8_t *>(id.data()), idLength) == olm_error())
        return false;
    const size_t keyLength = olm_outbound_group_session_key_length(session);
    QByteArray key(static_cast<int>(keyLength), Qt::Uninitialized);
    if (olm_outbound_group_session_key(session, reinterpret_cast<uint8_t *>(key.data()), keyLength) == olm_error())
        return false;
    const size_t pickleLength = olm_pickle_outbound_group_session_length(session);
    pickle.resize(static_cast<int>(pickleLength));
    static const QByteArray pickleKeyLiteral("matrix-megolm-outbound");
    if (olm_pickle_outbound_group_session(session, pickleKeyLiteral.constData(),
            pickleKeyLiteral.size(), pickle.data(), pickleLength) == olm_error())
        return false;
    sessionId = QString::fromUtf8(id);
    sessionKey = key;
    const_cast<MatrixOlmCrypto *>(this)->FOutboundMegolmPickles.insert(roomId, pickle);
    const_cast<MatrixOlmCrypto *>(this)->FOutboundMegolmSessions.insert(roomId, memory);
    const_cast<MatrixOlmCrypto *>(this)->FOutboundMegolmSessionIds.insert(roomId, sessionId);
    const_cast<MatrixOlmCrypto *>(this)->FOutboundMegolmSessionKeys.insert(roomId, sessionKey);
    return true;
#else
    Q_UNUSED(roomId); Q_UNUSED(sessionId); Q_UNUSED(sessionKey); Q_UNUSED(pickle);
    return false;
#endif
}

bool MatrixOlmCrypto::exportOutboundMegolmSession(MatrixDatabase &database, const QString &roomId,
                                                  QString &sessionId, QByteArray &sessionKey)
{
#if HAVE_OLM
    if (roomId.isEmpty())
        return false;
    QByteArray pickle;
    if (!database.loadOutboundMegolmSession(roomId, sessionId, pickle))
        return false;
    return exportOutboundMegolmSessionWithPickle(roomId, sessionId, pickle,
        sessionId, sessionKey);
#else
    Q_UNUSED(database); Q_UNUSED(roomId); Q_UNUSED(sessionId); Q_UNUSED(sessionKey);
    return false;
#endif
}

bool MatrixOlmCrypto::exportOutboundMegolmSessionWithPickle(const QString &roomId,
                                                            const QString &storedSessionId,
                                                            const QByteArray &pickle,
                                                            QString &sessionId,
                                                            QByteArray &sessionKey)
{
#if HAVE_OLM
    if (roomId.isEmpty())
        return false;
    if (FOutboundMegolmSessionIds.contains(roomId) &&
            FOutboundMegolmSessionKeys.contains(roomId)) {
        OlmOutboundGroupSession *cached = FOutboundMegolmSessions.contains(roomId)
            ? reinterpret_cast<OlmOutboundGroupSession *>(FOutboundMegolmSessions.value(roomId)->data()) : nullptr;
        const QString cachedId = outboundMegolmSessionId(cached);
        if (cachedId.isEmpty() || cachedId != FOutboundMegolmSessionIds.value(roomId)) {
            qWarning() << "[Matrix-E2EE] rejecting cached outbound Megolm session ID mismatch"
                       << "stored:" << FOutboundMegolmSessionIds.value(roomId)
                       << "actual:" << cachedId;
            FOutboundMegolmSessionIds.remove(roomId);
            FOutboundMegolmSessionKeys.remove(roomId);
            FOutboundMegolmSessions.remove(roomId);
            FOutboundMegolmPickles.remove(roomId);
        } else {
            sessionId = cachedId;
            sessionKey = FOutboundMegolmSessionKeys.value(roomId);
            return true;
        }
    }
    std::vector<char> localMemory;
    bool loadedFromPickle = false;
    if (!pickle.isEmpty()) {
        localMemory.resize(olm_outbound_group_session_size());
        OlmOutboundGroupSession *loaded = olm_outbound_group_session(localMemory.data());
        static const QByteArray pickleKeyLiteral("matrix-megolm-outbound");
        QByteArray mutablePickle = pickle;
        if (!loaded || olm_unpickle_outbound_group_session(loaded, pickleKeyLiteral.constData(),
                pickleKeyLiteral.size(), mutablePickle.data(), mutablePickle.size()) == olm_error())
            return false;
        loadedFromPickle = true;
    } else {
        if (!FOutboundMegolmSessions.contains(roomId))
            return false;
        localMemory.resize(olm_outbound_group_session_size());
        std::memcpy(localMemory.data(), FOutboundMegolmSessions.value(roomId)->constData(),
                    localMemory.size());
    }
    // Both branches contain an initialized session; the libolm constructor would reset it.
    OlmOutboundGroupSession *session = reinterpret_cast<OlmOutboundGroupSession *>(localMemory.data());
    const size_t idLength = olm_outbound_group_session_id_length(session);
    QByteArray id(static_cast<int>(idLength), Qt::Uninitialized);
    if (olm_outbound_group_session_id(session, reinterpret_cast<uint8_t *>(id.data()), idLength) == olm_error())
        return false;
    const size_t keyLength = olm_outbound_group_session_key_length(session);
    QByteArray key(static_cast<int>(keyLength), Qt::Uninitialized);
    if (olm_outbound_group_session_key(session, reinterpret_cast<uint8_t *>(key.data()), keyLength) == olm_error())
        return false;
    const QString actualSessionId = QString::fromUtf8(id);
    if (!storedSessionId.isEmpty() && storedSessionId != actualSessionId) {
        qWarning() << "[Matrix-E2EE] rejected outbound Megolm pickle ID mismatch:"
                   << "stored:" << storedSessionId << "actual:" << actualSessionId;
        return false;
    }
    sessionId = actualSessionId;
    sessionKey = key;
    FOutboundMegolmPickles.insert(roomId, pickle);
    const QSharedPointer<QByteArray> stableMemory = QSharedPointer<QByteArray>::create(localMemory);
    FOutboundMegolmSessions.insert(roomId, stableMemory);
    FOutboundMegolmSessionIds.insert(roomId, sessionId);
    FOutboundMegolmSessionKeys.insert(roomId, sessionKey);
    return true;
#else
    Q_UNUSED(roomId); Q_UNUSED(storedSessionId); Q_UNUSED(pickle);
    Q_UNUSED(sessionId); Q_UNUSED(sessionKey);
    return false;
#endif
}

int MatrixOlmCrypto::outboundMegolmMessageIndex(MatrixDatabase &database, const QString &roomId)
{
#if HAVE_OLM
    QString sessionId;
    QByteArray sessionKey;
    if (!exportOutboundMegolmSession(database, roomId, sessionId, sessionKey))
        return -1;
    return static_cast<int>(olm_outbound_group_session_message_index(
        reinterpret_cast<OlmOutboundGroupSession *>(FOutboundMegolmSessions[roomId]->data())));
#else
    Q_UNUSED(database); Q_UNUSED(roomId);
    return -1;
#endif
}

int MatrixOlmCrypto::outboundMegolmMessageIndexForLoadedSession(const QString &roomId) const
{
#if HAVE_OLM
    if (!FOutboundMegolmSessions.contains(roomId))
        return -1;
    return static_cast<int>(olm_outbound_group_session_message_index(
        reinterpret_cast<OlmOutboundGroupSession *>(
            const_cast<char *>(FOutboundMegolmSessions.value(roomId)->constData()))));
#else
    Q_UNUSED(roomId);
    return -1;
#endif
}

QByteArray MatrixOlmCrypto::encryptMegolm(MatrixDatabase &database, const QString &roomId,
                                          const QByteArray &plaintext, QString &sessionId)
{
#if HAVE_OLM
    QByteArray pickle;
    QByteArray message = encryptMegolmData(roomId, plaintext, sessionId, pickle);
    if (message.isEmpty())
        return QByteArray();
    return database.saveOutboundMegolmSession(roomId, sessionId, pickle) ? message : QByteArray();
#else
    Q_UNUSED(database); Q_UNUSED(roomId); Q_UNUSED(plaintext); Q_UNUSED(sessionId);
    return QByteArray();
#endif
}

QByteArray MatrixOlmCrypto::encryptMegolmData(const QString &roomId, const QByteArray &plaintext,
                                              QString &sessionId, QByteArray &pickle)
{
#if HAVE_OLM
    if (roomId.isEmpty() || plaintext.isEmpty())
        return QByteArray();
    if (!FOutboundMegolmPickles.contains(roomId) && !FOutboundMegolmSessions.contains(roomId))
        return QByteArray();
    std::vector<char> localMemory(olm_outbound_group_session_size());
    OlmOutboundGroupSession *session = nullptr;
    if (FOutboundMegolmPickles.contains(roomId)) {
        session = olm_outbound_group_session(localMemory.data());
        const QByteArray pickleKeyLiteral("matrix-megolm-outbound");
        QByteArray mutablePickle = FOutboundMegolmPickles.value(roomId);
        if (!session || olm_unpickle_outbound_group_session(session, pickleKeyLiteral.constData(),
                pickleKeyLiteral.size(), mutablePickle.data(), mutablePickle.size()) == olm_error())
            return QByteArray();
    } else {
        session = reinterpret_cast<OlmOutboundGroupSession *>(FOutboundMegolmSessions[roomId]->data());
    }
    if (sessionId.isEmpty())
        sessionId = FOutboundMegolmSessionIds.value(roomId);
    if (sessionId.isEmpty())
        return QByteArray();
    const size_t messageLength = olm_group_encrypt_message_length(session, plaintext.size());
    if (messageLength == olm_error())
        return QByteArray();
    QByteArray message(static_cast<int>(messageLength), Qt::Uninitialized);
    if (olm_group_encrypt(session, reinterpret_cast<const uint8_t *>(plaintext.constData()), plaintext.size(),
                          reinterpret_cast<uint8_t *>(message.data()), message.size()) == olm_error()) {
        qWarning() << "[Matrix-E2EE] outbound Megolm encryption failed:"
                   << olm_outbound_group_session_last_error(session)
                   << "room:" << roomId << "plaintextBytes:" << plaintext.size();
        return QByteArray();
    }
    const size_t pickleLength = olm_pickle_outbound_group_session_length(session);
    pickle.resize(static_cast<int>(pickleLength));
    static const QByteArray pickleKeyLiteral("matrix-megolm-outbound");
    if (olm_pickle_outbound_group_session(session, pickleKeyLiteral.constData(),
            pickleKeyLiteral.size(), pickle.data(), pickleLength) == olm_error())
        return QByteArray();
    FOutboundMegolmPickles.insert(roomId, pickle);
    FOutboundMegolmSessions.insert(roomId, QSharedPointer<QByteArray>::create(
        reinterpret_cast<const char *>(session), static_cast<int>(olm_outbound_group_session_size())));
    return message;
#else
    Q_UNUSED(roomId); Q_UNUSED(plaintext); Q_UNUSED(sessionId); Q_UNUSED(pickle);
    return QByteArray();
#endif
}

bool MatrixOlmCrypto::createSas(const QString &transactionId, QByteArray &publicKey)
{
#if HAVE_OLM
    if (transactionId.isEmpty())
        return false;
    QByteArray memory(static_cast<int>(olm_sas_size()), Qt::Uninitialized);
    OlmSAS *sas = olm_sas(memory.data());
    const size_t randomLength = olm_create_sas_random_length(sas);
    QByteArray random(static_cast<int>(randomLength), Qt::Uninitialized);
    for (char &byte : random)
        byte = static_cast<char>(QRandomGenerator::system()->generate() & 0xff);
    if (!sas || olm_create_sas(sas, random.data(), random.size()) == olm_error())
        return false;
    const size_t keyLength = olm_sas_pubkey_length(sas);
    publicKey.resize(static_cast<int>(keyLength));
    const size_t publicKeyResult = olm_sas_get_pubkey(sas, publicKey.data(), keyLength);
    if (publicKeyResult == olm_error()) {
        qWarning() << "Matrix libolm SAS public-key generation failed:"
                   << olm_sas_last_error(sas)
                   << "bufferLength:" << keyLength;
        return false;
    }
    while (publicKey.endsWith('='))
        publicKey.chop(1);
    FSasSessions.insert(transactionId, memory);
    FSasMacMethods.insert(transactionId, QStringLiteral("hkdf-hmac-sha256"));
    return true;
#else
    Q_UNUSED(transactionId); Q_UNUSED(publicKey);
    return false;
#endif
}

bool MatrixOlmCrypto::setSasTheirKey(const QString &transactionId, const QByteArray &theirKey)
{
#if HAVE_OLM
    if (theirKey.isEmpty())
        return false;
    auto sessionIt = FSasSessions.find(transactionId);
    if (sessionIt == FSasSessions.end())
        return false;
    QByteArray &session = sessionIt.value();
    OlmSAS *sas = reinterpret_cast<OlmSAS *>(session.data());
    QByteArray mutableKey = theirKey;
    const size_t result = olm_sas_set_their_key(sas, mutableKey.data(), mutableKey.size());
    const bool accepted = result != olm_error();
    qWarning() << "Matrix libolm SAS peer-key set:"
               << "result:" << result
               << "error:" << olm_sas_last_error(sas)
               << "keyLength:" << mutableKey.size()
               << "accepted:" << accepted
               << "theirKeySet:" << olm_sas_is_their_key_set(sas);
    return accepted && olm_sas_is_their_key_set(sas);
#else
    Q_UNUSED(transactionId); Q_UNUSED(theirKey);
    return false;
#endif
}

QByteArray MatrixOlmCrypto::generateSasBytes(const QString &transactionId, const QByteArray &info,
                                             int length)
{
#if HAVE_OLM
    if (info.isEmpty() || length <= 0)
        return QByteArray();
    auto sessionIt = FSasSessions.find(transactionId);
    if (sessionIt == FSasSessions.end())
        return QByteArray();
    QByteArray &session = sessionIt.value();
    OlmSAS *sas = reinterpret_cast<OlmSAS *>(session.data());
    qWarning() << "Matrix libolm SAS generation state:"
               << "theirKeySet:" << olm_sas_is_their_key_set(sas)
               << "sessionBytes:" << session.size();
    QByteArray output(length, Qt::Uninitialized);
    if (olm_sas_generate_bytes(sas, info.constData(), info.size(), output.data(), output.size()) == olm_error()) {
        qWarning() << "Matrix libolm SAS generation failed:"
                   << olm_sas_last_error(sas)
                   << "infoLength:" << info.size()
                   << "outputLength:" << output.size()
                   << "theirKeySet:" << olm_sas_is_their_key_set(sas);
        return QByteArray();
    }
    return output;
#else
    Q_UNUSED(transactionId); Q_UNUSED(info); Q_UNUSED(length);
    return QByteArray();
#endif
}

QByteArray MatrixOlmCrypto::calculateSasMac(const QString &transactionId, const QByteArray &input,
                                            const QByteArray &info)
{
#if HAVE_OLM
    if (!FSasSessions.contains(transactionId) || info.isEmpty()) {
        qWarning() << "Matrix libolm SAS MAC calculation skipped:"
                   << "sessionPresent:" << FSasSessions.contains(transactionId)
                   << "inputLength:" << input.size()
                   << "infoLength:" << info.size();
        return QByteArray();
    }
    OlmSAS *sas = reinterpret_cast<OlmSAS *>(FSasSessions[transactionId].data());
    const size_t macLength = olm_sas_mac_length(sas);
    QByteArray mac(static_cast<int>(macLength), Qt::Uninitialized);
    const bool fixedBase64 = FSasMacMethods.value(transactionId) == QStringLiteral("hkdf-hmac-sha256.v2");
    const size_t result = fixedBase64
        ? olm_sas_calculate_mac_fixed_base64(
            sas, input.constData(), input.size(), info.constData(), info.size(), mac.data(), mac.size())
        : olm_sas_calculate_mac(
            sas, input.constData(), input.size(), info.constData(), info.size(), mac.data(), mac.size());
    if (result == olm_error()) {
        qWarning() << "Matrix libolm SAS MAC calculation failed:"
                   << olm_sas_last_error(sas)
                   << "inputLength:" << input.size()
                   << "infoLength:" << info.size()
                   << "macBufferLength:" << mac.size()
                   << "theirKeySet:" << olm_sas_is_their_key_set(sas);
        return QByteArray();
    }
    Q_UNUSED(result); // libolm returns zero on success; mac already has the fixed length.
    return mac;
#else
    Q_UNUSED(transactionId); Q_UNUSED(input); Q_UNUSED(info);
    return QByteArray();
#endif
}

void MatrixOlmCrypto::setSasMacMethod(const QString &transactionId, const QString &method)
{
    if (!transactionId.isEmpty() &&
        (method == QStringLiteral("hkdf-hmac-sha256") ||
         method == QStringLiteral("hkdf-hmac-sha256.v2")))
        FSasMacMethods.insert(transactionId, method);
}

QString MatrixOlmCrypto::sasMacMethod(const QString &transactionId) const
{
    return FSasMacMethods.value(transactionId);
}

bool MatrixOlmCrypto::verifySasMac(const QString &transactionId, const QByteArray &input,
                                   const QByteArray &info, const QByteArray &expectedMac)
{
    if (expectedMac.isEmpty())
        return false;
    const QByteArray actualMac = calculateSasMac(transactionId, input, info);
    if (actualMac.size() != expectedMac.size())
        return false;
    unsigned char difference = 0;
    for (int i = 0; i < actualMac.size(); ++i)
        difference |= static_cast<unsigned char>(actualMac.at(i) ^ expectedMac.at(i));
    return difference == 0;
}

QByteArray MatrixOlmCrypto::sasCommitment(const QByteArray &publicKey, const QJsonObject &startContent) const
{
    if (publicKey.isEmpty() || startContent.isEmpty())
        return QByteArray();
    const QByteArray input = publicKey + canonicalJsonValue(startContent);
    return QCryptographicHash::hash(input, QCryptographicHash::Sha256)
        .toBase64(QByteArray::OmitTrailingEquals);
}

QStringList MatrixOlmCrypto::sasEmoji(const QByteArray &sasBytes) const
{
    QStringList result;
    if (sasBytes.size() < 6 || MatrixSasEmoji.size() != 64)
        return result;
    quint64 value = 0;
    for (int i = 0; i < 6; ++i)
        value = (value << 8) | static_cast<unsigned char>(sasBytes.at(i));
    for (int i = 0; i < 7; ++i) {
        const int shift = 48 - 6 * (i + 1);
        result.append(MatrixSasEmoji.at(static_cast<int>((value >> shift) & 0x3f)));
    }
    return result;
}

QString MatrixOlmCrypto::sasDecimal(const QByteArray &sasBytes) const
{
    if (sasBytes.size() < 5)
        return QString();
    const auto byte = [&sasBytes](int index) {
        return static_cast<unsigned char>(sasBytes.at(index));
    };
    const int first = ((byte(0) << 5) | (byte(1) >> 3)) + 1000;
    const int second = (((byte(1) & 0x07) << 10) | (byte(2) << 2) | (byte(3) >> 6)) + 1000;
    const int third = (((byte(3) & 0x3f) << 7) | (byte(4) >> 1)) + 1000;
    return QStringLiteral("%1 %2 %3").arg(first, 4, 10, QLatin1Char('0'))
        .arg(second, 4, 10, QLatin1Char('0')).arg(third, 4, 10, QLatin1Char('0'));
}