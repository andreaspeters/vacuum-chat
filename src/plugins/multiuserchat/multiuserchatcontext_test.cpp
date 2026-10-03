#include <interfaces/iaccountmanager.h>
#include "multiuserchatcontext.h"

class FakeAccount : public IAccount
{
public:
    explicit FakeAccount(ProtocolKind accountKind) : kind(accountKind) {}

    QObject *instance() override { return nullptr; }
    ProtocolKind protocolKind() const override { return kind; }
    Capabilities capabilities() const override { return CapabilityNone; }
    ConnectionState connectionState() const override { return StateDisconnected; }
    QUuid accountId() const override { return QUuid(); }
    bool isActive() const override { return false; }
    QString name() const override { return QString(); }
    OptionsNode optionsNode() const override { return OptionsNode(); }
    bool isValid() const override { return true; }
    void setActive(bool) override {}
    void setName(const QString &) override {}
    Jid streamJid() const override { return Jid(); }
    void setStreamJid(const Jid &) override {}
    QString password() const override { return QString(); }
    void setPassword(const QString &) override {}
    IXmppStream *xmppStream() const override { return nullptr; }

protected:
    void activeChanged(bool) override {}
    void optionsChanged(const OptionsNode &) override {}

private:
    ProtocolKind kind;
};

class FakeAccountManager : public IAccountManager
{
public:
    FakeAccountManager() : account(nullptr) {}

    QObject *instance() override { return nullptr; }
    QList<IAccount *> accounts() const override { return QList<IAccount *>(); }
    IAccount *accountById(const QUuid &) const override { return nullptr; }
    IAccount *accountByProtocolId(const AccountId &) const override { return nullptr; }
    IAccount *accountByStream(const Jid &streamJid) const override
    {
        return streamJid == expectedStream ? account : nullptr;
    }
    IAccount *appendAccount(const QUuid &) override { return nullptr; }
    void showAccount(const QUuid &) override {}
    void hideAccount(const QUuid &) override {}
    void removeAccount(const QUuid &) override {}
    void destroyAccount(const QUuid &) override {}

    IAccount *account;
    Jid expectedStream;

protected:
    void appended(IAccount *) override {}
    void shown(IAccount *) override {}
    void hidden(IAccount *) override {}
    void removed(IAccount *) override {}
    void changed(IAccount *, const OptionsNode &) override {}
    void destroyed(const QUuid &) override {}
};

int main()
{
    FakeAccount xmpp(IProtocolAccount::ProtocolXmpp);
    FakeAccount matrix(IProtocolAccount::ProtocolMatrix);
    FakeAccount meshcore(IProtocolAccount::ProtocolMeshCore);
    FakeAccountManager manager;
    manager.expectedStream = Jid("alice@example.org");
    manager.account = &xmpp;

    if (!isXmppProtocolAccount(&xmpp))
        return 1;
    if (isXmppProtocolAccount(&matrix))
        return 2;
    if (isXmppProtocolAccount(&meshcore))
        return 3;
    if (isXmppProtocolAccount(nullptr))
        return 4;
    if (!isXmppAccountForStream(&manager, manager.expectedStream))
        return 5;

    manager.account = &matrix;
    if (isXmppAccountForStream(&manager, manager.expectedStream))
        return 6;
    if (isXmppAccountForStream(&manager, Jid("wrong@example.org")))
        return 7;
    if (isXmppAccountForStream(nullptr, manager.expectedStream))
        return 8;
    return 0;
}
