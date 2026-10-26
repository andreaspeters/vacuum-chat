#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <QJsonParseError>

#include "matrixolm.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    MatrixOlmCrypto crypto;
    const QString userId = QStringLiteral("@e2ee-test:example.org");
    const QString deviceId = QStringLiteral("E2EE_TEST_DEVICE");
    QByteArray pickleKey;
    QByteArray pickle;
    if (!crypto.initializeNewAccount(userId, deviceId, pickleKey, pickle)) {
        qCritical() << "Olm account initialization failed";
        return 1;
    }
    if (!crypto.isInitialized()) {
        qCritical() << "Olm account was not initialized";
        return 1;
    }

    const QByteArray result = crypto.prepareKeysUpload(userId, deviceId, 1, false);
    if (result.isEmpty()) {
        qCritical() << "prepareKeysUpload returned an empty payload";
        return 1;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(result, &parseError);
    if (!doc.isObject()) {
        qCritical() << "Payload is not a JSON object:" << parseError.errorString();
        return 1;
    }

    const QJsonObject obj = doc.object();
    if (!obj.contains("device_keys")) {
        qCritical() << "Payload is missing device_keys";
        return 1;
    }

    if (!obj.contains("one_time_keys")) {
        qCritical() << "Payload is missing one_time_keys";
        return 1;
    }

    const QJsonObject deviceKeys = obj.value("device_keys").toObject();
    if (deviceKeys.value("user_id").toString() != userId ||
        deviceKeys.value("device_id").toString() != deviceId) {
        qCritical() << "Device key identity does not match the synthetic account";
        return 1;
    }

    const QJsonObject identityKeys = deviceKeys.value("keys").toObject();
    if (identityKeys.value(QStringLiteral("curve25519:%1").arg(deviceId)).toString().isEmpty() ||
        identityKeys.value(QStringLiteral("ed25519:%1").arg(deviceId)).toString().isEmpty()) {
        qCritical() << "Device identity keys are incomplete";
        return 1;
    }

    const QString signingKeyId = QStringLiteral("ed25519:%1").arg(deviceId);
    const QJsonObject deviceSignature = deviceKeys.value("signatures").toObject()
        .value(userId).toObject();
    if (deviceSignature.value(signingKeyId).toString().isEmpty()) {
        qCritical() << "Device keys are missing their Ed25519 signature";
        return 1;
    }

    const QJsonObject oneTimeKeys = obj.value("one_time_keys").toObject();
    if (oneTimeKeys.size() != 1) {
        qCritical() << "Expected one generated one-time key, got" << oneTimeKeys.size();
        return 1;
    }
    const QString oneTimeKeyId = oneTimeKeys.keys().constFirst();
    if (!oneTimeKeyId.startsWith(QStringLiteral("signed_curve25519:"))) {
        qCritical() << "Generated one-time key has an unexpected algorithm";
        return 1;
    }
    const QJsonObject oneTimeKey = oneTimeKeys.value(oneTimeKeyId).toObject();
    if (oneTimeKey.value("key").toString().isEmpty() ||
        oneTimeKey.value("signatures").toObject().value(userId).toObject()
            .value(signingKeyId).toString().isEmpty()) {
        qCritical() << "Generated one-time key is missing key material or its signature";
        return 1;
    }

    qInfo() << "Olm key upload payload test passed";
    return 0;
}
