#include "recentcontactsprotocolicon.h"
#include <QGuiApplication>
#include <QStringList>
#include <iostream>
#include <utils/filestorage.h>
#include <utils/iconstorage.h>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    FileStorage::setResourcesDirs(QList<QString>() << QString::fromLocal8Bit(RECENTCONTACTS_RESOURCE_DIR));

    using RecentContactsProtocolIcon::menuIconKey;
    int failures = 0;
    const auto expectKey = [&failures](IProtocolAccount::ProtocolKind kind, bool favorite,
                                       const QString &expected, const char *description) {
        if (menuIconKey(kind, favorite) != expected) {
            std::cerr << "RecentContacts protocol icon: " << description << '\n';
            ++failures;
        }
    };

    expectKey(IProtocolAccount::ProtocolXmpp, true, QStringLiteral("recentcontactsProtocolXmpp"),
              "favorites should use the official XMPP logo");
    expectKey(IProtocolAccount::ProtocolMatrix, true, QStringLiteral("recentcontactsProtocolMatrix"),
              "favorites should use the official Matrix logo");
    expectKey(IProtocolAccount::ProtocolMeshCore, true, QStringLiteral("recentcontactsProtocolMeshCore"),
              "favorites should use the official MeshCore logo");
    expectKey(IProtocolAccount::ProtocolXmpp, false, QString(),
              "non-favorites must not show a protocol logo");
    expectKey(IProtocolAccount::ProtocolMatrix, false, QString(),
              "non-favorites must not show a protocol logo");
    expectKey(IProtocolAccount::ProtocolMeshCore, false, QString(),
              "non-favorites must not show a protocol logo");

    const auto expectResource = [&failures](const QString &key, const char *description) {
        if (IconStorage::staticStorage(QStringLiteral("menuicons"))->getIcon(key).isNull()) {
            std::cerr << "RecentContacts protocol icon: missing icon resource: " << description << '\n';
            ++failures;
        }
    };
    expectResource(menuIconKey(IProtocolAccount::ProtocolXmpp, true), "XMPP");
    expectResource(menuIconKey(IProtocolAccount::ProtocolMatrix, true), "Matrix");
    expectResource(menuIconKey(IProtocolAccount::ProtocolMeshCore, true), "MeshCore");

    const QStringList accountIds = RecentContactsProtocolIcon::accountIdCandidates(
        QStringLiteral("stale-account-id"), QStringLiteral("matrix-account-id"), QStringLiteral("index-account-id"));
    if (accountIds != (QStringList() << QStringLiteral("stale-account-id")
                                     << QStringLiteral("matrix-account-id")
                                     << QStringLiteral("index-account-id"))) {
        std::cerr << "RecentContacts protocol icon: account lookup must try all IDs in priority order" << std::endl;
        ++failures;
    }
    const QStringList uniqueAccountIds = RecentContactsProtocolIcon::accountIdCandidates(
        QString(), QStringLiteral("matrix-account-id"), QStringLiteral("matrix-account-id"));
    if (uniqueAccountIds != (QStringList() << QStringLiteral("matrix-account-id"))) {
        std::cerr << "RecentContacts protocol icon: account lookup candidates must be unique" << std::endl;
        ++failures;
    }
    if (RecentContactsProtocolIcon::accountIdForRosterStream(
            QStringLiteral("matrix-user@example.org"), QStringLiteral("matrix-user@example.org"),
            QStringLiteral("matrix-account-uuid")) != QStringLiteral("matrix-account-uuid")) {
        std::cerr << "RecentContacts protocol icon: protocol stream must resolve to its account ID" << std::endl;
        ++failures;
    }
    if (!RecentContactsProtocolIcon::accountIdForRosterStream(
            QStringLiteral("meshcore-account"), QStringLiteral("different-stream"),
            QStringLiteral("meshcore-account-uuid")).isEmpty()) {
        std::cerr << "RecentContacts protocol icon: unmatched stream must not resolve an account" << std::endl;
        ++failures;
    }

    const quint32 protocolLabel = RecentContactsProtocolIcon::protocolLabelId();
    const QList<quint32> favoriteLabels = RecentContactsProtocolIcon::favoriteLabelIds();
    if (AdvancedDelegateItem::getPosition(protocolLabel) != AdvancedDelegateItem::MiddleCenter ||
        AdvancedDelegateItem::getFloor(protocolLabel) != 128 ||
        AdvancedDelegateItem::getOrder(protocolLabel) >=
            AdvancedDelegateItem::getOrder(AdvancedDelegateItem::DisplayId)) {
        std::cerr << "RecentContacts protocol icon: logo must precede the displayed name\n";
        ++failures;
    }
    if (favoriteLabels.size() != 1 || favoriteLabels.first() != protocolLabel) {
        std::cerr << "RecentContacts protocol icon: only the protocol logo should be an additional label\\n";
        ++failures;
    }
    if (!RecentContactsProtocolIcon::favoriteBulbVisible(true, false) ||
        RecentContactsProtocolIcon::favoriteBulbVisible(true, true) ||
        RecentContactsProtocolIcon::favoriteBulbVisible(false, true) ||
        RecentContactsProtocolIcon::favoriteBulbVisible(false, false)) {
        std::cerr << "favorite bulb: visible only for favorites without pending notifications\n";
        ++failures;
    }
    if (RecentContactsProtocolIcon::favoriteBulbVisible(true, false, true)) {
        std::cerr << "favorite pin: not shown on favorite roster rows\n";
        ++failures;
    }
    if (AdvancedDelegateItem::getPosition(AdvancedDelegateItem::DecorationId) != AdvancedDelegateItem::MiddleLeft) {
        std::cerr << "RecentContacts protocol icon: status lamp must stay in the default left decoration slot" << std::endl;
        ++failures;
    }

    return failures == 0 ? 0 : 1;
}