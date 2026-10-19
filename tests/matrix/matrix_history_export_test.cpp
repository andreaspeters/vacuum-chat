#include "matrix.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>

#include <iostream>

class MatrixSyncBatchTestAccess
{
public:
    static void setConfiguredUserId(Matrix &matrix, const QString &userId)
    {
        matrix.FDatabaseUserId = userId;
        matrix.FNetworkUserId.clear();
    }

    static void loadCachedHistory(Matrix &matrix, const QString &roomId,
        const QList<MatrixTimelineEvent> &events)
    {
        matrix.onCachedHistoryLoaded(roomId, events);
    }
};

namespace
{
bool check(bool condition, const char *description)
{
    if (!condition)
        std::cerr << "FAIL: " << description << '\n';
    return condition;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Matrix matrix;
    const QString roomId = QStringLiteral("!history-export:example.org");
    MatrixSyncBatchTestAccess::setConfiguredUserId(matrix, QStringLiteral("@me:example.org"));

    MatrixTimelineEvent later;
    later.roomId = roomId;
    later.eventId = QStringLiteral("$later");
    later.eventType = QStringLiteral("m.room.message");
    later.sender = QStringLiteral("@me:example.org");
    later.originTs = 2000;
    later.messageType = QStringLiteral("m.text");
    later.content = QStringLiteral("my cached message");

    MatrixTimelineEvent earlier;
    earlier.roomId = roomId;
    earlier.eventId = QStringLiteral("$earlier");
    earlier.eventType = QStringLiteral("m.room.message");
    earlier.sender = QStringLiteral("@other:example.org");
    earlier.originTs = 1000;
    earlier.messageType = QStringLiteral("m.text");
    earlier.content = QStringLiteral("other cached message");

    QList<BasicMessage> emittedMessages;
    int historyLoadedCount = 0;
    QEventLoop loop;
    QObject::connect(&matrix, &Matrix::protocolMessageReceived, &matrix,
        [&emittedMessages](const BasicMessage &message) { emittedMessages.append(message); });
    QObject::connect(&matrix, &Matrix::protocolHistoryLoaded, &loop,
        [&loop, &historyLoadedCount, &roomId](const QString &loadedRoomId) {
            if (loadedRoomId == roomId) {
                ++historyLoadedCount;
                loop.quit();
            }
        });
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);

    MatrixSyncBatchTestAccess::loadCachedHistory(matrix, roomId, {later, earlier});
    loop.exec();

    if (!check(historyLoadedCount == 1,
            "cached Matrix history emits one completion signal after its messages") ||
        !check(emittedMessages.size() == 2,
            "cached Matrix history exposes both events through IProtocolMessaging") ||
        !check(emittedMessages.at(0).messageId() == earlier.eventId &&
            emittedMessages.at(1).messageId() == later.eventId,
            "cached Matrix messages are emitted chronologically") ||
        !check(emittedMessages.at(0).protocol() == QStringLiteral("matrix") &&
            emittedMessages.at(0).conversationId() == roomId &&
            emittedMessages.at(0).metadata().value(QStringLiteral("historical")).toBool(),
            "cached history preserves protocol-neutral message identity and historical metadata") ||
        !check(emittedMessages.at(1).metadata().value(QStringLiteral("sender_is_self")).toBool() &&
            !emittedMessages.at(0).metadata().value(QStringLiteral("sender_is_self")).toBool(),
            "cached history preserves own-sender identity for the shared renderer"))
        return 1;

    std::cout << "Matrix history export tests passed\n";
    return 0;
}
