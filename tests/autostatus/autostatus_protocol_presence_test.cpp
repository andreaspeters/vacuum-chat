#include "autostatusprotocolpresence.h"

#include <interfaces/iprotocolpresence.h>

#include <iostream>

class FakeProtocolPresence : public IProtocolPresence
{
public:
    QObject *instance() override { return &object; }
    QString streamId() const override { return id; }
    int show() const override { return currentShow; }
    QString status() const override { return currentStatus; }
    bool setPresence(int newShow, const QString &newStatus) override
    {
        ++setCalls;
        if (!acceptUpdates)
            return false;
        currentShow = newShow;
        currentStatus = newStatus;
        return true;
    }

    QObject object;
    QString id = QStringLiteral("@alice:example.org");
    int currentShow = 1;
    QString currentStatus = QStringLiteral("Original status");
    int setCalls = 0;
    bool acceptUpdates = true;
};

int main()
{
    int failures = 0;
    const auto expect = [&failures](bool condition, const char *message) {
        if (!condition) {
            std::cerr << "AutoStatus protocol presence: " << message << '\n';
            ++failures;
        }
    };

    AutoStatusInternal::ProtocolPresenceController controller;
    FakeProtocolPresence presence;
    const QString providerKey = QStringLiteral("provider/stream");

    expect(controller.apply(&presence, providerKey, true, 3,
                            QStringLiteral("Automatic away")),
           "online presence should accept the automatic rule");
    expect(presence.show() == 3 &&
               presence.status() == QStringLiteral("Automatic away") &&
               presence.setCalls == 1,
           "automatic show and status text should be applied once");

    controller.apply(&presence, providerKey, false, 3,
                     QStringLiteral("Automatic away"));
    expect(presence.setCalls == 1,
           "reapplying an unchanged rule should not send another presence update");

    expect(controller.apply(&presence, providerKey, false, 4,
                            QStringLiteral("Automatic busy")),
           "a changed automatic rule should update the provider");
    expect(presence.show() == 4 &&
               presence.status() == QStringLiteral("Automatic busy") &&
               presence.setCalls == 2,
           "changed rule should update both show and text");

    expect(controller.restore(&presence, providerKey),
           "disabling AutoStatus should restore the prior presence");
    expect(presence.show() == 1 &&
               presence.status() == QStringLiteral("Original status") &&
               presence.setCalls == 3,
           "restore should preserve the original show and status text");
    expect(!controller.restore(&presence, providerKey) && presence.setCalls == 3,
           "restoring twice should not issue another update");

    FakeProtocolPresence offlinePresence;
    offlinePresence.currentShow = 0;
    expect(!controller.apply(&offlinePresence, QStringLiteral("provider/offline"),
                             false, 3, QStringLiteral("Automatic away")) &&
               offlinePresence.setCalls == 0,
           "offline presence should not be changed by an idle rule");

    return failures == 0 ? 0 : 1;
}
