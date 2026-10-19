#ifndef PROTOCOLHISTORYPROVIDERBINDING_H
#define PROTOCOLHISTORYPROVIDERBINDING_H

#include <QObject>

#include <interfaces/imessage.h>

namespace ProtocolHistoryProviderBinding
{
inline void bind(QObject *provider, QObject *consumer, bool providesHistoryLoadedSignal)
{
    if (!provider || !consumer)
        return;

    QObject::connect(provider, SIGNAL(protocolMessageReceived(BasicMessage)),
        consumer, SLOT(onProtocolMessageReceived(BasicMessage)), Qt::UniqueConnection);
    if (providesHistoryLoadedSignal)
        QObject::connect(provider, SIGNAL(protocolHistoryLoaded(QString)),
            consumer, SLOT(onProtocolHistoryLoaded(QString)), Qt::UniqueConnection);
}
}

#endif // PROTOCOLHISTORYPROVIDERBINDING_H
