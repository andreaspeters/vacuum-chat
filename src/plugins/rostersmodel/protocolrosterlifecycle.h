#ifndef PROTOCOLROSTERLIFECYCLE_H
#define PROTOCOLROSTERLIFECYCLE_H

#include <QString>

namespace ProtocolRosterLifecycle
{
struct StreamTransition
{
    QString streamIdToClose;
    bool shouldBuildStream;
};

inline StreamTransition transition(const QString &previousStreamId, const QString &streamId)
{
    return {previousStreamId != streamId ? previousStreamId : QString(), !streamId.isEmpty()};
}
}

#endif
