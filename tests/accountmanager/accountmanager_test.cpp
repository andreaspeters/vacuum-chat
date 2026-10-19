#include "protocolaccountrootresolver.h"

#include <interfaces/identity.h>
#include <interfaces/iprotocolroster.h>

#include <iostream>

namespace {
class SyntheticProtocolRoster final : public IProtocolRoster
{
public:
    SyntheticProtocolRoster(const AccountId &accountId, const QString &streamId)
        : FAccountId(accountId), FStreamId(streamId)
    {
    }

    QString accountId() const override { return FAccountId; }
    QString protocol() const override { return QStringLiteral("synthetic"); }
    QString streamId() const override { return FStreamId; }
    QList<ProtocolRosterEntry> entries() const override { return QList<ProtocolRosterEntry>(); }
    QList<ProtocolRoom> rooms() const override { return QList<ProtocolRoom>(); }
    ProtocolRosterEntry entry(const QString &) const override { return ProtocolRosterEntry(); }
    ProtocolRoom room(const QString &) const override { return ProtocolRoom(); }

private:
    AccountId FAccountId;
    QString FStreamId;
};

bool check(bool condition, const char *description)
{
    if (!condition)
        std::cerr << "FAIL: " << description << '\n';
    return condition;
}
}

int main()
{
    const AccountId matrixAccountId = QStringLiteral("00000000-0000-4000-8000-000000000017");
    const AccountId meshcoreAccountId = QStringLiteral("00000000-0000-4000-8000-000000000018");
    const QString matrixStreamId = QStringLiteral("matrix-stream-@alice:example.org");
    const QString meshcoreStreamId = QStringLiteral("meshcore-radio-73");
    SyntheticProtocolRoster matrixRoster(matrixAccountId, matrixStreamId);
    SyntheticProtocolRoster meshcoreRoster(meshcoreAccountId, meshcoreStreamId);
    const QList<IProtocolRoster *> providers = { &matrixRoster, &meshcoreRoster };
    bool passed = true;

    const AccountId resolved = ProtocolAccountRootResolver::persistentAccountIdForStreamId(
        matrixStreamId, providers);
    passed &= check(resolved == matrixAccountId,
        "protocol root stream ID resolves to its persistent account ID");
    passed &= check(resolved != matrixStreamId,
        "resolver does not use the protocol stream ID as the account ID");
    passed &= check(ProtocolAccountRootResolver::persistentAccountIdForStreamId(
        QString(), providers).isEmpty(), "empty root stream ID does not resolve an account");
    passed &= check(ProtocolAccountRootResolver::persistentAccountIdForStreamId(
        QStringLiteral("unknown-stream"), providers).isEmpty(),
        "unknown root stream ID does not resolve an account");

    SyntheticProtocolRoster duplicateA(QStringLiteral("account-a"), QStringLiteral("duplicate-stream"));
    SyntheticProtocolRoster duplicateB(QStringLiteral("account-b"), QStringLiteral("duplicate-stream"));
    passed &= check(ProtocolAccountRootResolver::persistentAccountIdForStreamId(
        QStringLiteral("duplicate-stream"), { &duplicateA, &duplicateB }).isEmpty(),
        "ambiguous stream IDs fail closed instead of selecting the wrong account");

    return passed ? 0 : 1;
}
