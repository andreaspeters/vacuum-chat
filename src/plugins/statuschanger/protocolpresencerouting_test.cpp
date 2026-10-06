#include "protocolpresencerouting.h"

#include <iostream>

namespace
{
class MockProtocolPresence final : public IProtocolPresence
{
public:
    explicit MockProtocolPresence(const QString &id, int value)
        : accountId(id), currentShow(value) {}

    QObject *instance() override { return nullptr; }
    QString streamId() const override { return accountId; }
    int show() const override { return currentShow; }
    QString status() const override { return QString(); }
    bool setPresence(int value, const QString &) override
    {
        currentShow = value;
        return true;
    }

private:
    QString accountId;
    int currentShow;
};

bool check(bool condition, const char *message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}
}

int main()
{
    MockProtocolPresence matrix(QStringLiteral("matrix-account"), 1);
    MockProtocolPresence another(QStringLiteral("another-account"), 2);
    const QList<IProtocolPresence *> providers{&matrix, &another};
    bool passed = true;

    passed &= check(ProtocolPresenceRouting::providerForAccountId(providers,
        QStringLiteral("another-account")) == &another,
        "account IDs route to their matching provider");
    passed &= check(ProtocolPresenceRouting::providerForAccountId(providers,
        QStringLiteral("missing-account")) == nullptr,
        "unknown account IDs have no provider");
    passed &= check(ProtocolPresenceRouting::providerForAccountId(providers,
        QString()) == nullptr, "empty account IDs have no provider");

    passed &= check(ProtocolPresenceRouting::setPresenceForAccountId(providers,
        QStringLiteral("another-account"), 3, QStringLiteral("busy")),
        "status changes are sent to the matching provider");
    passed &= check(another.show() == 3 && matrix.show() == 1,
        "account status routing does not affect other providers");
    passed &= check(!ProtocolPresenceRouting::setPresenceForAccountId(providers,
        QStringLiteral("missing-account"), 3, QStringLiteral("busy")),
        "status changes for unsupported accounts are harmless");
    passed &= check(!ProtocolPresenceRouting::setPresenceForAccountId({},
        QStringLiteral("matrix-account"), 3, QStringLiteral("busy")),
        "no providers safely rejects an unsupported status change");
    return passed ? 0 : 1;
}
