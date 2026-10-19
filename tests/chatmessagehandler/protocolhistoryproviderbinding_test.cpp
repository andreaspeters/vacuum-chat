#include <QCoreApplication>
#include <iostream>

#include <interfaces/imessage.h>
#include "protocolhistoryproviderbinding.h"

class FakeHistoryProvider : public QObject
{
    Q_OBJECT
public:
    void publish()
    {
        emit protocolMessageReceived(BasicMessage());
        emit protocolHistoryLoaded(QStringLiteral("conversation"));
    }

signals:
    void protocolMessageReceived(BasicMessage message);
    void protocolHistoryLoaded(QString conversationId);
};

class FakeHistoryConsumer : public QObject
{
    Q_OBJECT
public:
    int messageCount = 0;
    int historyLoadedCount = 0;

public slots:
    void onProtocolMessageReceived(BasicMessage)
    {
        ++messageCount;
    }

    void onProtocolHistoryLoaded(QString)
    {
        ++historyLoadedCount;
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    FakeHistoryProvider provider;
    FakeHistoryConsumer consumer;

    ProtocolHistoryProviderBinding::bind(&provider, &consumer, true);
    ProtocolHistoryProviderBinding::bind(&provider, &consumer, true);
    provider.publish();

    const bool passed = consumer.messageCount == 1 && consumer.historyLoadedCount == 1;
    if (!passed)
        std::cerr << "history provider signals should reach the consumer exactly once\n";
    return passed ? 0 : 1;
}

#include "protocolhistoryproviderbinding_test.moc"
