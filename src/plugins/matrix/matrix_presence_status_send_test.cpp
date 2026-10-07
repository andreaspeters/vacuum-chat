#include "matrix.h"
#include "matrixnetwork.h"

#include <interfaces/iaccountmanager.h>
#include <interfaces/ipresence.h>
#include <utils/options.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDomDocument>
#include <QEventLoop>
#include <QUuid>

#include <iostream>

class MatrixPresenceLifecycleTestAccess
{
public:
    static void setNetwork(Matrix &matrix, MatrixNetwork *network)
    {
        matrix.FMatrixNetwork = network;
    }

    static void showAccount(Matrix &matrix, IAccount *account)
    {
        matrix.onAccountShown(account);
    }

    static void detachNetwork(Matrix &matrix)
    {
        matrix.FMatrixNetwork = nullptr;
    }
};

class SyntheticMatrixNetwork final : public MatrixNetwork
{
    Q_OBJECT

public:
    int loginCalls = 0;
    int passwordLoginCalls = 0;
    int presenceCalls = 0;
    int logoutCalls = 0;
    quint64 loginGeneration = 0;
    QString requestedLoginUser;

    Q_INVOKABLE void loginWithAccessTokenForSession(const QString &, const QString &,
        const QString &, quint64 generation)
    {
        ++loginCalls;
        loginGeneration = generation;
    }

    Q_INVOKABLE QString loginForSession(const QString &userId, const QString &,
        const QString &, quint64 generation)
    {
        ++passwordLoginCalls;
        requestedLoginUser = userId;
        loginGeneration = generation;
        return QString();
    }

    Q_INVOKABLE void setPresence(const QString &, const QString &, const QString &)
    {
        ++presenceCalls;
    }

    Q_INVOKABLE void sync(bool) {}
    Q_INVOKABLE void sync() {}

    Q_INVOKABLE void logout()
    {
        ++logoutCalls;
    }

    void finishLogin(const QString &userId)
    {
        emit loginSuccessForSession(userId, QStringLiteral("synthetic-token"),
            QStringLiteral("synthetic-device"), loginGeneration);
    }
};

class SyntheticMatrixAccount final : public QObject, public IAccount
{
public:
    explicit SyntheticMatrixAccount(const QUuid &id, const QString &username)
        : FId(id), FOptions(Options::node(QStringLiteral("accounts.account"),
              id.toString(QUuid::WithoutBraces)))
    {
        FOptions.setValue(QStringLiteral("matrix"), QStringLiteral("type"));
        FOptions.setValue(QStringLiteral("http://example.invalid"), QStringLiteral("matrix.instance"));
        FOptions.setValue(username, QStringLiteral("matrix.username"));
        FOptions.setValue(QStringLiteral("synthetic-device"), QStringLiteral("matrix.device-id"));
    }

    QObject *instance() override { return this; }
    bool isValid() const override { return true; }
    QUuid accountId() const override { return FId; }
    ProtocolKind protocolKind() const override { return ProtocolMatrix; }
    Capabilities capabilities() const override { return CapabilityChat; }
    ConnectionState connectionState() const override { return StateDisconnected; }
    bool isActive() const override { return true; }
    void setActive(bool) override {}
    QString name() const override { return QStringLiteral("Synthetic Matrix account"); }
    void setName(const QString &) override {}
    Jid streamJid() const override { return Jid(); }
    void setStreamJid(const Jid &) override {}
    QString password() const override { return QStringLiteral("synthetic-password"); }
    void setPassword(const QString &) override {}
    OptionsNode optionsNode() const override { return FOptions; }
    IXmppStream *xmppStream() const override { return nullptr; }

protected:
    void activeChanged(bool) override {}
    void optionsChanged(const OptionsNode &) override {}

private:
    QUuid FId;
    OptionsNode FOptions;
};

namespace
{
bool check(bool condition, const char *description)
{
    if (!condition)
        std::cerr << "FAIL: " << description << '\n';
    return condition;
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QDomDocument optionsDocument;
    optionsDocument.appendChild(optionsDocument.createElement(QStringLiteral("options")));
    Options::setOptions(optionsDocument, QString(), QByteArray());

    const QString loginUser = QStringLiteral("status-send");
    const QString userId = QStringLiteral("@status-send:example.invalid");
    qputenv("MATRIX_ACCESS_TOKEN", QByteArrayLiteral("synthetic-access-token"));
    qputenv("MATRIX_USER_ID", userId.toUtf8());

    SyntheticMatrixAccount account(QUuid::createUuid(), loginUser);
    Matrix matrix;
    SyntheticMatrixNetwork network;
    MatrixPresenceLifecycleTestAccess::setNetwork(matrix, &network);
    MatrixPresenceLifecycleTestAccess::showAccount(matrix, &account);
    application.processEvents(QEventLoop::AllEvents);

    const QString roomId = QStringLiteral("!status-send:example.invalid");
    const BasicMessage message(QStringLiteral("unused-id"), roomId, userId, QString(),
        QStringLiteral("status-gated message"), QDateTime::currentDateTimeUtc(),
        QStringLiteral("matrix"), BasicMessage::Outgoing);

    bool passed = check(network.loginCalls == 0 && network.passwordLoginCalls == 0,
            "account activation alone does not log Matrix in") &&
        check(!matrix.sendMessage(message),
            "Matrix rejects message sends while the account is offline") &&
        check(matrix.setPresence(IPresence::Online, QStringLiteral("ready")),
            "an explicit Online presence request is accepted");

    application.processEvents(QEventLoop::AllEvents);
    passed = check(network.passwordLoginCalls == 1 &&
            network.requestedLoginUser == loginUser,
        "Online presence logs in with the configured localpart") && passed;
    passed = check(matrix.show() != IPresence::Online,
        "Matrix remains offline until login succeeds") && passed;

    network.finishLogin(userId);
    application.processEvents(QEventLoop::AllEvents);
    passed = check(matrix.show() == IPresence::Online,
        "successful login applies the requested Online status") && passed;

    bool localEchoReceived = false;
    BasicMessage localEcho;
    QObject::connect(&matrix, &Matrix::protocolMessageReceived, &matrix,
        [&localEchoReceived, &localEcho](const BasicMessage &echo) {
            localEchoReceived = true;
            localEcho = echo;
        });
    passed = check(matrix.sendMessage(message),
        "Matrix accepts an outgoing message after login") && passed;
    passed = check(localEchoReceived && localEcho.body() == message.body() &&
            localEcho.conversationId() == roomId && localEcho.direction() == BasicMessage::Outgoing,
        "the accepted message produces a protocol-neutral local echo") && passed;

    passed = check(matrix.setPresence(IPresence::Offline, QString()),
        "an explicit Offline presence request is accepted") && passed;
    passed = check(matrix.show() == IPresence::Offline && !matrix.sendMessage(message),
        "Offline immediately blocks further Matrix message sends") && passed;
    application.processEvents(QEventLoop::AllEvents);
    passed = check(network.logoutCalls == 1,
        "Offline presence dispatches exactly one Matrix logout") && passed;

    MatrixPresenceLifecycleTestAccess::detachNetwork(matrix);
    qunsetenv("MATRIX_ACCESS_TOKEN");
    qunsetenv("MATRIX_USER_ID");

    if (passed)
        std::cout << "PASS: Matrix presence controls login and message-send availability\n";
    return passed ? 0 : 1;
}

#include "matrix_presence_status_send_test.moc"
