#include <interfaces/iprotocolroster.h>
#include "rosterclipboardidentifierpolicy.h"

#include <iostream>

class EmptyProtocolRoster : public IProtocolRoster
{
public:
    QString accountId() const override { return QString(); }
    QString protocol() const override { return QString(); }
    QString streamId() const override { return QString(); }
    QList<ProtocolRosterEntry> entries() const override { return {}; }
    QList<ProtocolRoom> rooms() const override { return {}; }
    ProtocolRosterEntry entry(const QString &) const override { return {}; }
    ProtocolRoom room(const QString &) const override { return {}; }
};

int main()
{
    const EmptyProtocolRoster roster;
    const auto identifier = roster.accountIdentifier();
    if (!identifier.label.isEmpty() || !identifier.value.isEmpty()) {
        std::cerr << "providers without a native account identifier must default to empty\n";
        return 1;
    }

    const auto xmpp = RosterClipboardIdentifierPolicy::resolve(
        {QStringLiteral("Jabber ID"), QStringLiteral("alice@example.org")}, QString());
    if (xmpp.source != RosterClipboardIdentifierPolicy::Source::NativeAccount ||
        xmpp.label != QStringLiteral("Jabber ID") ||
        xmpp.value != QStringLiteral("alice@example.org")) {
        std::cerr << "XMPP account identifier was not preserved for copy\n";
        return 1;
    }

    const auto matrix = RosterClipboardIdentifierPolicy::resolve(
        {QStringLiteral("Matrix user ID"), QStringLiteral("@alice:example.org")}, QString());
    if (matrix.source != RosterClipboardIdentifierPolicy::Source::NativeAccount ||
        matrix.label != QStringLiteral("Matrix user ID") ||
        matrix.value != QStringLiteral("@alice:example.org")) {
        std::cerr << "Matrix account identifier was not preserved for copy\n";
        return 1;
    }

    const auto meshCore = RosterClipboardIdentifierPolicy::resolve(
        {QStringLiteral("MeshCore public key"), QStringLiteral(
             "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f")}, QString());
    if (meshCore.source != RosterClipboardIdentifierPolicy::Source::NativeAccount ||
        meshCore.label != QStringLiteral("MeshCore public key") ||
        meshCore.value != QStringLiteral(
            "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f")) {
        std::cerr << "MeshCore public key was not preserved for copy\n";
        return 1;
    }

    const auto conversation = RosterClipboardIdentifierPolicy::resolve(
        ProtocolAccountIdentifier(), QStringLiteral("!room:example.org"));
    if (conversation.source != RosterClipboardIdentifierPolicy::Source::Conversation ||
        conversation.value != QStringLiteral("!room:example.org")) {
        std::cerr << "conversation identifier fallback was not preserved for copy\n";
        return 1;
    }
    return 0;
}
