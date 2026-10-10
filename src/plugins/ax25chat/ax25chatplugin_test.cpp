#include "ax25chatplugin.h"

#include <utils/jid.h>

#include <QCoreApplication>
#include <QPluginLoader>

#include <iostream>

namespace
{
bool check(bool condition, const char *message)
{
    if (!condition)
        std::cerr << message << '\n';
    return condition;
}
}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    Ax25ChatPlugin plugin;
    bool passed = true;
    passed &= check(plugin.protocol() == QStringLiteral("ax25"),
                    "AX.25 plugin must expose the ax25 protocol identifier");

    ConversationId conversationId;
    passed &= check(plugin.conversationIdForAddress(
                        Jid::fromUserInput(QStringLiteral("dl1aaa-7@ax25.invalid")),
                        conversationId),
                    "a valid synthetic AX.25 address must map to a conversation");
    passed &= check(conversationId == QStringLiteral("DL1AAA-7"),
                    "conversation identity must be the canonical remote callsign");

    const Jid address = plugin.addressForConversation(conversationId);
    passed &= check(address.isValid() && address.node() == QStringLiteral("DL1AAA-7") &&
                        address.domain() == QStringLiteral("ax25.invalid"),
                    "conversation address mapping must round-trip through a synthetic JID");

    ConversationId rejectedId;
    passed &= check(!plugin.conversationIdForAddress(
                        Jid::fromUserInput(QStringLiteral("DL1AAA-7@example.org")), rejectedId),
                    "addresses outside the AX.25 adapter domain must be rejected");
    passed &= check(!plugin.conversationIdForAddress(
                        Jid::fromUserInput(QStringLiteral("DL1 AAA@ax25.invalid")), rejectedId),
                    "invalid callsigns must be rejected");
    passed &= check(!plugin.addressForConversation(QStringLiteral("DL1AAA-16")).isValid(),
                    "conversation IDs with an out-of-range SSID must not produce an address");

    if (argc > 1) {
        QPluginLoader loader(QString::fromLocal8Bit(argv[1]));
        QObject *loadedPlugin = loader.instance();
        if (!loadedPlugin) {
            std::cerr << "could not load AX.25 plugin: " << loader.errorString().toStdString() << '\n';
            return 1;
        }
        passed &= check(loader.metaData().value(QStringLiteral("IID")).toString() ==
                            QStringLiteral("Vacuum.Core.IPlugin/1.0"),
                        "plugin metadata must use Vacuum's IPlugin IID");
        passed &= check(qobject_cast<IPlugin *>(loadedPlugin) != nullptr,
                        "loaded plugin must expose IPlugin");
        passed &= check(qobject_cast<IProtocolMessaging *>(loadedPlugin) != nullptr,
                        "loaded plugin must expose protocol-neutral messaging");
        passed &= check(qobject_cast<IProtocolRoster *>(loadedPlugin) != nullptr,
                        "loaded plugin must expose protocol-neutral roster");
        passed &= check(qobject_cast<IProtocolPresence *>(loadedPlugin) != nullptr,
                        "loaded plugin must expose protocol-neutral presence");
        passed &= check(qobject_cast<IProtocolCapabilities *>(loadedPlugin) != nullptr,
                        "loaded plugin must expose protocol capabilities");
        passed &= check(qobject_cast<IProtocolContactActions *>(loadedPlugin) != nullptr,
                        "loaded plugin must expose its supported add-contact action");
    }

    return passed ? 0 : 1;
}
