#ifndef MATRIXSSSS_H
#define MATRIXSSSS_H

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

class MatrixSsss
{
public:
    static bool derivePassphraseKey(const QString &passphrase,
                                    const QJsonObject &keyDescription,
                                    QByteArray &key);
    static bool deriveRecoveryKey(const QString &recoveryKey,
                                  const QJsonObject &keyDescription,
                                  QByteArray &key);
    static bool decryptSecret(const QJsonObject &encrypted,
                              const QByteArray &key,
                              const QString &secretName,
                              QByteArray &plaintext);
    static bool encryptSecret(const QByteArray &plaintext,
                              const QByteArray &key,
                              const QString &secretName,
                              QJsonObject &encrypted);
    static bool decryptExportedSessions(const QByteArray &fileData,
                                        const QString &passphrase,
                                        QJsonDocument &sessions,
                                        QString &error);
};

#endif
