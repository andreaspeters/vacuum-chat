#ifndef MATRIXOLM_H
#define MATRIXOLM_H

#include <QByteArray>
#include <QString>
#include <QHash>
#include <QSharedPointer>
#include <QStringList>
#include <QJsonObject>
#include <cstdint>

class MatrixDatabase;

struct MatrixMegolmDecryptResult
{
    QByteArray plaintext;
    uint32_t messageIndex = 0;
    QString status;
    QString error;

    bool succeeded() const { return status == QStringLiteral("decrypted"); }
};

class MatrixOlmCrypto
{
public:
    MatrixOlmCrypto();
    ~MatrixOlmCrypto();

    bool initialize(MatrixDatabase &database, const QString &userId, const QString &deviceId);
    bool initializeFromStoredAccount(const QString &userId, const QString &deviceId,
                                     const QByteArray &pickleKey, const QByteArray &pickle);
    bool initializeNewAccount(const QString &userId, const QString &deviceId,
                              QByteArray &pickleKey, QByteArray &pickle);
    bool isInitialized() const { return FAccount != nullptr; }
    void clearOlmSessions();
    void clearOlmSession(const QString &userId, const QString &deviceId);
    QStringList oneTimeKeyIds() const;
    QStringList fallbackKeyIds() const;
    QByteArray identityKeysJson() const;
    QByteArray canonicalJson(const QJsonObject &object) const;
    QByteArray signCanonicalJson(const QByteArray &canonicalJson) const;
    QByteArray signWithPkSeed(const QByteArray &seed, const QByteArray &canonicalJson,
                              QByteArray &publicKey) const;
    bool verifyDeviceKeys(const QString &userId, const QString &deviceId,
                         const QJsonObject &deviceKeys) const;
    bool verifySignedOneTimeKey(const QString &userId, const QString &deviceId,
                                const QString &signingKey, const QJsonObject &oneTimeKey) const;
    bool verifyCrossSigningKey(const QString &userId, const QJsonObject &keyObject,
                               const QString &signingKeyId, const QString &signingKey) const;
    bool verifyDeviceCrossSignature(const QString &userId, const QJsonObject &deviceKeys,
                                    const QString &signingKeyId,
                                    const QString &signingKey) const;
    QByteArray prepareKeysUpload(const QString &userId, const QString &deviceId,
                                 int count = 50, bool replaceFallback = false);
    void markKeysAsPublished();
    bool persistAccount(MatrixDatabase &database);
    bool importMegolmSession(MatrixDatabase &database, const QString &sessionId,
                             const QString &roomId, const QString &senderKey,
                             const QByteArray &sessionKey);
    bool prepareMegolmSession(const QString &sessionId, const QByteArray &sessionKey,
                              QByteArray &pickle);
    bool prepareExportedMegolmSession(const QString &sessionId, const QByteArray &sessionKey,
                                      QByteArray &pickle);
    void clearMegolmSession(const QString &sessionId);
    QByteArray decryptMegolm(MatrixDatabase &database, const QString &sessionId,
                             const QByteArray &ciphertext);
    MatrixMegolmDecryptResult decryptMegolmDetailed(MatrixDatabase &database,
                                                    const QString &sessionId,
                                                    const QByteArray &ciphertext);
    MatrixMegolmDecryptResult decryptMegolmDetailedWithPickle(
        const QString &sessionId, const QByteArray &ciphertext,
        const QByteArray &pickle);
    bool persistMegolmSession(MatrixDatabase &database, const QString &sessionId,
                              const QString &roomId, const QString &senderKey);
    QByteArray exportMegolmSession(MatrixDatabase &database, const QString &sessionId,
                                   uint32_t messageIndex = 0);
    QByteArray exportMegolmSessionWithPickle(const QString &sessionId,
                                             const QByteArray &pickle,
                                             uint32_t messageIndex = 0);
    bool createOlmSession(MatrixDatabase &database, const QString &userId,
                          const QString &deviceId, const QByteArray &identityKey,
                          const QByteArray &oneTimeKey);
    bool createOlmSessionData(const QString &userId, const QString &deviceId,
                              const QByteArray &identityKey, const QByteArray &oneTimeKey,
                              QString &sessionId, QByteArray &pickle);
    QByteArray encryptOlm(MatrixDatabase &database, const QString &userId,
                          const QString &deviceId, const QByteArray &plaintext,
                          int &messageType);
    QByteArray encryptOlmData(const QString &userId, const QString &deviceId,
                              const QByteArray &plaintext, int &messageType,
                              QString &sessionId, QByteArray &pickle);
    QByteArray decryptOlm(MatrixDatabase &database, const QString &senderUserId,
                          const QString &senderKey, int messageType,
                          const QByteArray &ciphertext, QString &sessionId,
                          const QString &senderDeviceId = QString());
    QString olmSessionId(MatrixDatabase &database, const QString &userId,
                         const QString &deviceId) const;
    QString olmSessionIdWithPickle(const QString &userId, const QString &deviceId,
                                   const QString &storedSessionId, const QByteArray &pickle);
    bool createOutboundMegolmSession(MatrixDatabase &database, const QString &roomId,
                                     QString &sessionId, QByteArray &sessionKey);
    bool createOutboundMegolmSessionData(const QString &roomId, QString &sessionId,
                                         QByteArray &sessionKey, QByteArray &pickle) const;
    bool exportOutboundMegolmSession(MatrixDatabase &database, const QString &roomId,
                                     QString &sessionId, QByteArray &sessionKey);
    bool exportOutboundMegolmSessionWithPickle(const QString &roomId, const QString &storedSessionId,
                                               const QByteArray &pickle, QString &sessionId,
                                               QByteArray &sessionKey);
    int outboundMegolmMessageIndex(MatrixDatabase &database, const QString &roomId);
    int outboundMegolmMessageIndexForLoadedSession(const QString &roomId) const;
    QByteArray encryptMegolm(MatrixDatabase &database, const QString &roomId,
                             const QByteArray &plaintext, QString &sessionId);
    QByteArray encryptMegolmData(const QString &roomId, const QByteArray &plaintext,
                                 QString &sessionId, QByteArray &pickle);
    bool createSas(const QString &transactionId, QByteArray &publicKey);
    bool setSasTheirKey(const QString &transactionId, const QByteArray &theirKey);
    QByteArray generateSasBytes(const QString &transactionId, const QByteArray &info,
                                int length = 5);
    QByteArray calculateSasMac(const QString &transactionId, const QByteArray &input,
                               const QByteArray &info);
    void setSasMacMethod(const QString &transactionId, const QString &method);
    QString sasMacMethod(const QString &transactionId) const;
    bool verifySasMac(const QString &transactionId, const QByteArray &input,
                      const QByteArray &info, const QByteArray &expectedMac);
    QByteArray sasCommitment(const QByteArray &publicKey, const QJsonObject &startContent) const;
    QStringList sasEmoji(const QByteArray &sasBytes) const;
    QString sasDecimal(const QByteArray &sasBytes) const;

private:
    void clear();
    bool createAccount();
    bool pickleAccount(MatrixDatabase &database);
    bool pickleAccountData(QByteArray &pickle) const;
    bool unpickleAccount(const QByteArray &pickle, const QByteArray &pickleKey);

    void *FAccount;
    QByteArray FPickleKey;
    QString FUserId;
    QString FDeviceId;
    QHash<QString, QSharedPointer<QByteArray>> FMegolmSessions;
    QHash<QString, QByteArray> FOutboundMegolmPickles;
    QHash<QString, QSharedPointer<QByteArray>> FOutboundMegolmSessions;
    QHash<QString, QString> FOutboundMegolmSessionIds;
    QHash<QString, QByteArray> FOutboundMegolmSessionKeys;
    QHash<QString, QByteArray> FOlmSessionPickles;
    QHash<QString, QSharedPointer<QByteArray>> FOlmSessions;
    QHash<QString, QByteArray> FSasSessions;
    QHash<QString, QString> FSasMacMethods;
};

#endif // MATRIXOLM_H
