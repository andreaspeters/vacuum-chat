#include "ax25chatplugin.h"
#include "aprsmessagecodec.h"
#include "aprsroomstore.h"
#include "ax25kisscodec.h"

#include <interfaces/iaccountmanager.h>
#include <interfaces/ipresence.h>
#include <interfaces/iprotocolroomactions.h>
#include <utils/jid.h>
#include <utils/options.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QPluginLoader>
#include <QThread>
#include <QTemporaryDir>

#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <utility>
#include <unistd.h>

class Ax25ChatPluginTestAccess
{
public:
    static void activate(Ax25ChatPlugin &plugin, IAccount *account, IPluginManager *manager)
    {
        plugin.m_pluginManager = manager;
        plugin.activateAccount(account);
    }

    static bool addGroup(Ax25ChatPlugin &plugin, const QString &groupName, QString *error = nullptr)
    {
        return plugin.addGroupSubscription(groupName, error);
    }

    static void receive(Ax25ChatPlugin &plugin, const QString &source,
                        const QString &messageId, const QString &text)
    {
        plugin.onIncomingMessage(source, messageId, text);
    }

};

class TestAccount final : public IAccount
{
public:
    TestAccount() : m_id(QUuid::createUuid()), m_options(Options::node(QString()))
    {
        m_options.setValue(QStringLiteral("N0CALL"), QStringLiteral("ax25.callsign"));
        m_options.setValue(QStringLiteral("kiss-serial"), QStringLiteral("ax25.transport"));
        m_options.setValue(115200, QStringLiteral("ax25.baud-rate"));
    }

    void setOption(const QVariant &value, const QString &path)
    {
        m_options.setValue(value, path);
    }

    QObject *instance() override { return nullptr; }
    ProtocolKind protocolKind() const override { return ProtocolAx25; }
    Capabilities capabilities() const override { return Capabilities(CapabilityChat); }
    ConnectionState connectionState() const override { return StateDisconnected; }
    QUuid accountId() const override { return m_id; }
    bool isActive() const override { return true; }
    QString name() const override { return QStringLiteral("test"); }
    OptionsNode optionsNode() const override { return m_options; }
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
    QUuid m_id;
    OptionsNode m_options;
};

class TestPluginManager final : public IPluginManager
{
public:
    explicit TestPluginManager(QString homePath) : m_homePath(std::move(homePath)) {}
    QObject *instance() override { return nullptr; }
    QString version() const override { return QString(); }
    QString revision() const override { return QString(); }
    QDateTime revisionDate() const override { return QDateTime(); }
    bool isShutingDown() const override { return false; }
    QString homePath() const override { return m_homePath; }
    void setHomePath(const QString &path) override { m_homePath = path; }
    void setLocale(QLocale::Language, QLocale::Country) override {}
    IPlugin *pluginInstance(const QUuid &) const override { return nullptr; }
    QList<IPlugin *> pluginInterface(const QString &) const override { return {}; }
    const IPluginInfo *pluginInfo(const QUuid &) const override { return nullptr; }
    QList<QUuid> pluginDependencesOn(const QUuid &) const override { return {}; }
    QList<QUuid> pluginDependencesFor(const QUuid &) const override { return {}; }
    void quit() override {}
    void restart() override {}
    void delayShutdown() override {}
    void continueShutdown() override {}

protected:
    void aboutToQuit() override {}
    void shutdownStarted() override {}

private:
    QString m_homePath;
};

namespace
{
bool check(bool condition, const char *message)
{
    if (!condition)
        std::cerr << message << '\n';
    return condition;
}

bool waitForUiFrame(QCoreApplication &application, int masterFd, int timeoutMs,
                    Ax25Kiss::Ax25UiFrame *uiFrame);

bool testGroupRoomIntegration()
{
    QTemporaryDir profile;
    if (!profile.isValid())
        return check(false, "could not create temporary APRS profile");

    QDomDocument options;
    options.appendChild(options.createElement(QStringLiteral("options")));
    Options::setOptions(options, QString(), QByteArray());
    TestAccount account;
    if (!check(account.optionsNode().value(QStringLiteral("ax25.callsign")).toString() ==
                   QStringLiteral("N0CALL"),
               "test account must expose its configured local callsign"))
        return false;
    TestPluginManager manager(profile.path());
    Ax25ChatPlugin plugin;
    int initOrder = 0;
    plugin.initConnections(&manager, initOrder);
    Ax25ChatPluginTestAccess::activate(plugin, &account, &manager);

    const QString accountId = account.accountId().toString();
    const QString settingsPath = profile.filePath(QStringLiteral("ax25chat.ini"));
    QString error;
    if (!AprsRoom::GroupStore::addGroup(settingsPath, accountId, QStringLiteral("SOTA"), &error))
        return check(false, "could not seed an APRS group subscription");

    const QString groupId = QStringLiteral("aprs-group:SOTA");
    const ProtocolRoom room = plugin.room(groupId);
    if (!check(room.id == groupId && room.name == QStringLiteral("SOTA") && room.isJoined &&
                   !room.isDirect && room.membership == QStringLiteral("desired"),
               "stored ANSRVR groups must appear as desired, non-direct rooms"))
        return false;

    IProtocolCapabilities *capabilities = qobject_cast<IProtocolCapabilities *>(&plugin);
    if (!check(capabilities && capabilities->hasCapabilities(
                   accountId, IProtocolCapabilities::CapabilityJoinRoom) &&
                   capabilities->hasCapabilities(accountId,
                       IProtocolCapabilities::CapabilityLeaveRoom, groupId),
               "room action capabilities must be scoped to the account and stored group"))
        return false;

    IProtocolRoomActions *actions = qobject_cast<IProtocolRoomActions *>(&plugin);
    if (!check(actions && actions->leaveRoom(accountId, groupId) &&
                   plugin.room(groupId).id.isEmpty(),
               "leaving an offline room must remove its persisted subscription"))
        return false;
    if (!Ax25ChatPluginTestAccess::addGroup(plugin, QStringLiteral("SOTA"), &error))
        return check(false, "joining offline must persist a desired group subscription");

    QList<BasicMessage> received;
    QObject::connect(&plugin, &Ax25ChatPlugin::protocolMessageReceived,
                     &plugin, [&received](const BasicMessage &message) {
        received.append(message);
    });
    Ax25ChatPluginTestAccess::receive(plugin, QStringLiteral("DL1AAA-7"),
        QStringLiteral("42"), QStringLiteral("CQ SOTA summit report"));
    if (!check(received.size() == 1 && received.first().conversationId() == groupId &&
                   received.first().sender() == QStringLiteral("DL1AAA-7") &&
                   received.first().body() == QStringLiteral("summit report"),
               "a subscribed CQ group relay must enter its room with the member as sender"))
        return false;

    Ax25ChatPluginTestAccess::receive(plugin, QStringLiteral("DL1AAA-7"),
        QStringLiteral("43"), QStringLiteral("CQ OTHER not subscribed"));
    if (!check(received.size() == 1,
               "a group relay for an unsubscribed room must not become a direct chat"))
        return false;

    Ax25ChatPluginTestAccess::receive(plugin, QStringLiteral("ANSRVR"),
        QStringLiteral("44"), QStringLiteral("Joined SOTA"));
    if (!check(received.size() == 2 && received.last().conversationId() == QStringLiteral("ANSRVR") &&
                   received.last().body() == QStringLiteral("Joined SOTA"),
               "ANSRVR status must remain distinct from member posts in a group room"))
        return false;

    const int masterFd = ::posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (!check(masterFd >= 0 && ::grantpt(masterFd) == 0 && ::unlockpt(masterFd) == 0,
               "synthetic serial PTY can be created for reconnect testing"))
        return false;
    char slavePath[256] = {};
    if (!check(::ptsname_r(masterFd, slavePath, sizeof(slavePath)) == 0,
               "synthetic serial PTY slave path is available")) {
        ::close(masterFd);
        return false;
    }
    account.setOption(QString::fromLocal8Bit(slavePath), QStringLiteral("ax25.port"));
    if (!check(plugin.setPresence(IPresence::Online, QString()),
               "connecting the configured test account must bring up its KISS transport")) {
        ::close(masterFd);
        return false;
    }

    Ax25Kiss::Ax25UiFrame joinFrame;
    AprsMessage::Message joinMessage;
    const bool sentInitialJoin = waitForUiFrame(*QCoreApplication::instance(), masterFd, 500,
                                                 &joinFrame) &&
        joinFrame.source == QStringLiteral("N0CALL") &&
        joinFrame.destination == QStringLiteral("APRS") &&
        AprsMessage::Codec::decode(joinFrame.information, &joinMessage) &&
        joinMessage.destination == QStringLiteral("ANSRVR") &&
        joinMessage.body == QStringLiteral("J SOTA");
    if (!check(sentInitialJoin,
               "saved desired groups must be joined on the KISS connection with an APRS command")) {
        plugin.setPresence(IPresence::Offline, QString());
        ::close(masterFd);
        return false;
    }

    const BasicMessage outgoing(QStringLiteral("group-outgoing"), groupId,
        QStringLiteral("N0CALL"), groupId, QStringLiteral("summit report"),
        QDateTime::currentDateTimeUtc(), QStringLiteral("ax25"), BasicMessage::Outgoing);
    Ax25Kiss::Ax25UiFrame groupFrame;
    AprsMessage::Message groupMessage;
    const bool sentGroupPost = plugin.sendMessage(outgoing) &&
        waitForUiFrame(*QCoreApplication::instance(), masterFd, 500, &groupFrame) &&
        groupFrame.source == QStringLiteral("N0CALL") &&
        groupFrame.destination == QStringLiteral("APRS") &&
        AprsMessage::Codec::decode(groupFrame.information, &groupMessage) &&
        groupMessage.destination == QStringLiteral("ANSRVR") &&
        groupMessage.body == QStringLiteral("CQ SOTA summit report");
    if (!check(sentGroupPost,
               "plugin group sends must use ANSRVR with a CQ <GROUP> body")) {
        plugin.setPresence(IPresence::Offline, QString());
        ::close(masterFd);
        return false;
    }

    plugin.setPresence(IPresence::Offline, QString());
    if (!check(plugin.setPresence(IPresence::Online, QString()),
               "the synthetic KISS transport must reconnect after going offline")) {
        ::close(masterFd);
        return false;
    }
    Ax25Kiss::Ax25UiFrame reconnectFrame;
    AprsMessage::Message reconnectMessage;
    const bool resentJoin = waitForUiFrame(*QCoreApplication::instance(), masterFd, 500,
                                            &reconnectFrame) &&
        reconnectFrame.destination == QStringLiteral("APRS") &&
        AprsMessage::Codec::decode(reconnectFrame.information, &reconnectMessage) &&
        reconnectMessage.destination == QStringLiteral("ANSRVR") &&
        reconnectMessage.body == QStringLiteral("J SOTA");
    plugin.setPresence(IPresence::Offline, QString());
    ::close(masterFd);
    return check(resentJoin,
                 "saved desired groups must be rejoined after transport reconnect");
}

bool waitForUiFrame(QCoreApplication &application, int masterFd, int timeoutMs,
                    Ax25Kiss::Ax25UiFrame *uiFrame)
{
    Ax25Kiss::KissStreamDecoder decoder;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        application.processEvents();
        char bytes[512];
        const ssize_t count = ::read(masterFd, bytes, sizeof(bytes));
        if (count > 0) {
            const QList<QByteArray> packets = decoder.feed(QByteArray(bytes, static_cast<int>(count)));
            for (const QByteArray &packet : packets)
                if (Ax25Kiss::Ax25UiFrameCodec::decodeUiFrame(packet, uiFrame))
                    return true;
        } else if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            return false;
        }
        QThread::msleep(1);
    }
    return false;
}
}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    Ax25ChatPlugin plugin;
    bool passed = true;
    passed &= check(plugin.protocol() == QStringLiteral("ax25"),
                    "AX.25 plugin must expose the ax25 protocol identifier");
    passed &= check(qobject_cast<IProtocolRoomActions *>(&plugin) != nullptr,
                    "AX.25 plugin must expose generic room actions for ANSRVR groups");

    ConversationId conversationId;
    passed &= check(plugin.conversationIdForAddress(
                        Jid::fromUserInput(QStringLiteral("dl1aaa-7@ax25.invalid")),
                        conversationId),
                    "a valid synthetic AX.25 address must map to a conversation");
    passed &= check(conversationId == QStringLiteral("DL1AAA-7"),
                    "conversation identity must be the canonical remote callsign");

    ConversationId groupConversationId;
    passed &= check(plugin.conversationIdForAddress(
                        Jid::fromUserInput(QStringLiteral("sota@group.ax25.invalid")),
                        groupConversationId),
                    "an ANSRVR group address must map to a room conversation");
    passed &= check(groupConversationId == QStringLiteral("aprs-group:SOTA"),
                    "group conversation IDs must be namespaced and canonicalized");
    const Jid groupAddress = plugin.addressForConversation(groupConversationId);
    passed &= check(groupAddress.isValid() && groupAddress.node() == QStringLiteral("SOTA") &&
                        groupAddress.domain() == QStringLiteral("group.ax25.invalid"),
                    "group conversations must map back to a distinct synthetic room address");
    passed &= check(plugin.conversationDisplayName(groupConversationId) == QStringLiteral("SOTA"),
                    "group conversation names must not expose the internal ID prefix");

    ConversationId ssidOne;
    ConversationId ssidTwo;
    passed &= check(plugin.conversationIdForAddress(
                        Jid::fromUserInput(QStringLiteral("DC6AP-1@ax25.invalid")), ssidOne) &&
                    plugin.conversationIdForAddress(
                        Jid::fromUserInput(QStringLiteral("DC6AP-2@ax25.invalid")), ssidTwo) &&
                    ssidOne == QStringLiteral("DC6AP-1") &&
                    ssidTwo == QStringLiteral("DC6AP-2") && ssidOne != ssidTwo,
                    "the same AX.25 callsign with different SSIDs maps to distinct contacts");

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
    passed &= testGroupRoomIntegration();

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
        passed &= check(qobject_cast<IProtocolRoomActions *>(loadedPlugin) != nullptr,
                        "loaded plugin must expose generic ANSRVR room actions");
    }

    return passed ? 0 : 1;
}
