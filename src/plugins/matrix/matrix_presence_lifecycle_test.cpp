#include "matrix.h"
#include "matrixnetwork.h"

#include <interfaces/iaccountmanager.h>
#include <interfaces/ipresence.h>
#include <interfaces/iprotocolprofileactions.h>
#include <utils/options.h>

#include <QApplication>
#include <QDomDocument>
#include <QEventLoop>
#include <QInputDialog>
#include <QTimer>
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

    static void hideAccount(Matrix &matrix, IAccount *account)
    {
        matrix.onAccountHidden(account);
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
    int presenceCalls = 0;
    int logoutCalls = 0;
    int displayNameUpdateCalls = 0;
    QString requestedDisplayName;
    QList<quint64> loginGenerations;

    Q_INVOKABLE QString login(const QString &, const QString &, const QString &)
    {
        ++loginCalls;
        return QString();
    }

    Q_INVOKABLE void loginWithAccessToken(const QString &, const QString &, const QString &)
    {
        ++loginCalls;
    }

    Q_INVOKABLE QString loginForSession(const QString &, const QString &, const QString &, quint64 generation)
    {
        ++loginCalls;
        loginGenerations.append(generation);
        return QString();
    }

    Q_INVOKABLE void loginWithAccessTokenForSession(const QString &, const QString &, const QString &,
        quint64 generation)
    {
        ++loginCalls;
        loginGenerations.append(generation);
    }

    Q_INVOKABLE void sync() {}
    Q_INVOKABLE void sync(bool) {}

    Q_INVOKABLE void setPresence(const QString &, const QString &, const QString &)
    {
        ++presenceCalls;
    }

    Q_INVOKABLE void logout()
    {
        ++logoutCalls;
    }

    Q_INVOKABLE void setOwnDisplayName(const QString &displayName)
    {
        ++displayNameUpdateCalls;
        requestedDisplayName = displayName;
    }

    void finishLogin(const QString &userId = QStringLiteral("@lifecycle:example.invalid"))
    {
        finishLoginForGeneration(loginGenerations.last(), userId);
    }

    void finishLoginForGeneration(quint64 generation,
        const QString &userId = QStringLiteral("@lifecycle:example.invalid"))
    {
        emit loginSuccessForSession(userId, QStringLiteral("synthetic-token"),
            QStringLiteral("synthetic-device"), generation);
    }
};

class SyntheticMatrixAccount final : public QObject, public IAccount
{
public:
    explicit SyntheticMatrixAccount(const QUuid &id,
        const QString &username = QStringLiteral("@lifecycle:example.invalid"))
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
    QApplication application(argc, argv);
    qunsetenv("MATRIX_ACCESS_TOKEN");
    qunsetenv("MATRIX_USER_ID");
    QDomDocument optionsDocument;
    optionsDocument.appendChild(optionsDocument.createElement(QStringLiteral("options")));
    Options::setOptions(optionsDocument, QString(), QByteArray());

    Matrix matrix;
    SyntheticMatrixNetwork network;
    SyntheticMatrixAccount account(QUuid::createUuid());
    IProtocolProfileActions *profileActions = qobject_cast<IProtocolProfileActions *>(matrix.instance());
    MatrixPresenceLifecycleTestAccess::setNetwork(matrix, &network);

    int initialOfflinePresenceChanges = 0;
    QString initialPresenceStream;
    QObject::connect(&matrix, &Matrix::protocolPresenceChanged, &matrix,
        [&initialOfflinePresenceChanges, &initialPresenceStream](const QString &streamId, int show, const QString &) {
            if (show == IPresence::Offline) {
                ++initialOfflinePresenceChanges;
                initialPresenceStream = streamId;
            }
        });

    MatrixPresenceLifecycleTestAccess::showAccount(matrix, &account);
    application.processEvents(QEventLoop::AllEvents);
    const bool initialOfflinePresenceAnnounced = check(
        initialOfflinePresenceChanges == 1 && !initialPresenceStream.isEmpty() &&
            matrix.show() == IPresence::Offline,
        "showing a Matrix account publishes its initial Offline state to generic presence consumers");
    const bool noImplicitLogin = check(network.loginCalls == 0,
        "activating an enabled Matrix account must prepare it without logging in");
    const bool canSetPresenceWhileDisconnected = check(
        matrix.hasCapabilities(matrix.accountId(), IProtocolCapabilities::CapabilitySetPresence),
        "an active disconnected Matrix account exposes the connect-capable presence action");
    const bool cannotEditProfileBeforeLogin = check(
        !matrix.hasCapabilities(matrix.accountId(), IProtocolCapabilities::CapabilityEditProfile),
        "Matrix does not expose profile editing before authentication");
    const bool canSendImageWhileDisconnected = check(
        matrix.hasCapabilities(matrix.accountId(), IProtocolCapabilities::CapabilitySendImage),
        "Matrix advertises image support for an active account even before login");
    const bool canSendImageForChatWindowStreamId = check(
        matrix.hasCapabilities(matrix.streamId(), IProtocolCapabilities::CapabilitySendImage),
        "Matrix accepts its messaging stream ID used by generic chat windows");
    const bool profileActionsExposed = check(profileActions != nullptr,
        "Matrix exposes the protocol-neutral profile action interface");
    const bool profileEditRejectedBeforeLogin = check(!profileActions ||
        !profileActions->editProfile(matrix.accountId()),
        "Matrix rejects profile editing before authentication");

    int onlinePresenceChanges = 0;
    int offlinePresenceChanges = 0;
    QObject::connect(&matrix, &Matrix::protocolPresenceChanged, &matrix,
        [&onlinePresenceChanges, &offlinePresenceChanges](const QString &, int show, const QString &) {
            if (show == IPresence::Online)
                ++onlinePresenceChanges;
            if (show == IPresence::Offline)
                ++offlinePresenceChanges;
        });
    const bool onlineRequestAccepted = check(
        matrix.setPresence(IPresence::Online, QStringLiteral("ready")),
        "an explicit Online request is accepted while Matrix is disconnected");
    const bool repeatedRequestAccepted = check(
        matrix.setPresence(IPresence::Online, QStringLiteral("ready")),
        "a repeated Online request is accepted while login is pending");
    application.processEvents(QEventLoop::AllEvents);
    const bool loginStarted = check(network.loginCalls == 1,
        "repeated Online requests start exactly one Matrix login");
    const bool remainsOfflineUntilLoginSuccess = check(
        matrix.show() != IPresence::Online && onlinePresenceChanges == 0,
        "Matrix does not report Online before loginSuccess");
    if (loginStarted)
        network.finishLogin();
    application.processEvents(QEventLoop::AllEvents);
    const bool canEditProfileDuringInitialSync = check(
        matrix.hasCapabilities(matrix.accountId(), IProtocolCapabilities::CapabilityEditProfile),
        "authenticated Matrix account can edit its profile before initial sync completes");
    const bool canSendImageDuringInitialSync = check(
        matrix.hasCapabilities(matrix.accountId(), IProtocolCapabilities::CapabilitySendImage),
        "authenticated Matrix account can send images before initial sync completes");
    bool profileEditAccepted = false;
    bool profileUpdateDispatched = false;
    if (profileActions) {
        bool profileDialogPresented = false;
        QTimer::singleShot(0, &application, [&profileDialogPresented]() {
            for (QWidget *widget : QApplication::topLevelWidgets()) {
                QInputDialog *dialog = qobject_cast<QInputDialog *>(widget);
                if (dialog) {
                    profileDialogPresented = true;
                    dialog->setTextValue(QStringLiteral("Updated Matrix display name"));
                    dialog->accept();
                    return;
                }
            }
        });
        profileEditAccepted = check(profileActions->editProfile(matrix.accountId()),
            "authenticated Matrix profile action opens and accepts a display-name edit");
        application.processEvents(QEventLoop::AllEvents);
        profileUpdateDispatched = check(profileDialogPresented && network.displayNameUpdateCalls == 1 &&
            network.requestedDisplayName == QStringLiteral("Updated Matrix display name"),
            "accepted Matrix display-name edit is queued to the network adapter");
    }
    const bool reportsOnlineAfterLoginSuccess = check(
        matrix.show() == IPresence::Online && matrix.status() == QStringLiteral("ready") &&
            onlinePresenceChanges == 1,
        "Matrix reports Online after loginSuccess");
    const bool remotePresenceUpdated = check(network.presenceCalls == 1,
        "Matrix publishes the requested presence after loginSuccess");
    const bool offlineAccepted = check(matrix.setPresence(IPresence::Offline, QString()),
        "Offline is accepted for a logged-in Matrix account");
    application.processEvents(QEventLoop::AllEvents);
    const bool sessionDisconnected = check(network.logoutCalls == 1,
        "Offline requests MatrixNetwork logout");
    const bool offlineReportedImmediately = check(
        matrix.show() == IPresence::Offline && offlinePresenceChanges == 1,
        "Matrix reports Offline immediately after the disconnect request");

    const bool oldAccountLoginAccepted = check(
        matrix.setPresence(IPresence::Online, QStringLiteral("old account")),
        "a disconnected old account can begin an explicit login");
    application.processEvents(QEventLoop::AllEvents);
    const quint64 oldLoginGeneration = network.loginGenerations.last();
    const int loginCallsBeforeSwitch = network.loginCalls;
    SyntheticMatrixAccount nextAccount(QUuid::createUuid());
    MatrixPresenceLifecycleTestAccess::hideAccount(matrix, &account);
    application.processEvents(QEventLoop::AllEvents);
    MatrixPresenceLifecycleTestAccess::showAccount(matrix, &nextAccount);
    application.processEvents(QEventLoop::AllEvents);
    const bool switchDoesNotAutoLogin = check(network.loginCalls == loginCallsBeforeSwitch,
        "switching to another active Matrix account does not auto-login");
    const int loginCallsAfterLateResult = network.loginCalls;
    const bool newAccountLoginAccepted = check(
        matrix.setPresence(IPresence::Online, QStringLiteral("new account")),
        "the newly selected Matrix account can start its own login");
    application.processEvents(QEventLoop::AllEvents);
    const bool newAccountLoginStarted = check(network.loginCalls == loginCallsAfterLateResult + 1,
        "the new account login is dispatched once");
    const quint64 newLoginGeneration = network.loginGenerations.last();
    network.finishLoginForGeneration(oldLoginGeneration);
    application.processEvents(QEventLoop::AllEvents);
    const bool lateOldLoginIgnored = check(
        matrix.show() != IPresence::Online && onlinePresenceChanges == 1,
        "late login success from the hidden Matrix account is ignored despite matching user IDs");
    network.finishLoginForGeneration(newLoginGeneration);
    application.processEvents(QEventLoop::AllEvents);
    const bool newAccountLoginCompleted = check(
        matrix.show() == IPresence::Online && onlinePresenceChanges == 2,
        "the current account can still complete its own login after a stale response");

    MatrixPresenceLifecycleTestAccess::detachNetwork(matrix);
    return initialOfflinePresenceAnnounced && noImplicitLogin && canSetPresenceWhileDisconnected && cannotEditProfileBeforeLogin && canSendImageWhileDisconnected && canSendImageForChatWindowStreamId &&
        profileActionsExposed && profileEditRejectedBeforeLogin && canEditProfileDuringInitialSync &&
        canSendImageDuringInitialSync &&
        profileEditAccepted && profileUpdateDispatched && onlineRequestAccepted &&
        repeatedRequestAccepted && loginStarted &&
        remainsOfflineUntilLoginSuccess && reportsOnlineAfterLoginSuccess && remotePresenceUpdated &&
        offlineAccepted && sessionDisconnected && offlineReportedImmediately && oldAccountLoginAccepted &&
        switchDoesNotAutoLogin && newAccountLoginAccepted && newAccountLoginStarted &&
        lateOldLoginIgnored && newAccountLoginCompleted ? 0 : 1;
}

#include "matrix_presence_lifecycle_test.moc"
