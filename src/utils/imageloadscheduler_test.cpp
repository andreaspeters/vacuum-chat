#include <utils/imageloadscheduler.h>

#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QList>
#include <QPointer>
#include <QSharedPointer>
#include <QStringList>
#include <QTimer>

#include <iostream>

namespace {
QStringList capturedDebugMessages;

int fail(const char *message)
{
    std::cerr << message << '\n';
    return 1;
}

void captureDebugMessage(QtMsgType type, const QMessageLogContext &, const QString &message)
{
    if (type == QtDebugMsg)
        capturedDebugMessages.append(message);
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    ImageLoadScheduler *scheduler = ImageLoadScheduler::instance();
    if (!scheduler)
        return fail("image load scheduler was not created");

    const bool debugEnabled = application.arguments().contains(QStringLiteral("--debug"));
    capturedDebugMessages.clear();
    const QtMessageHandler previousMessageHandler = qInstallMessageHandler(captureDebugMessage);
    QEventLoop decodeLoop;
    QPointer<QEventLoop> guardedDecodeLoop(&decodeLoop);
    QTimer decodeTimeout;
    decodeTimeout.setSingleShot(true);
    QObject::connect(&decodeTimeout, &QTimer::timeout, &decodeLoop, &QEventLoop::quit);
    struct DecodeResult {
        bool completed = false;
        bool nullImage = false;
    };
    const QSharedPointer<DecodeResult> decodeResult(new DecodeResult);
    scheduler->decodeDataAsync(QByteArrayLiteral("not-an-image"),
        [decodeResult, guardedDecodeLoop](const QImage &image) {
            decodeResult->completed = true;
            decodeResult->nullImage = image.isNull();
            if (guardedDecodeLoop)
                guardedDecodeLoop->quit();
        });
    decodeTimeout.start(5000);
    decodeLoop.exec();
    decodeTimeout.stop();
    qInstallMessageHandler(previousMessageHandler);
    if (!decodeResult->completed || !decodeResult->nullImage)
        return fail("invalid image data did not complete with a null image");
    const bool hasDecodeDiagnostic = capturedDebugMessages.join(QLatin1Char('\n'))
        .contains(QStringLiteral("ImageLoadScheduler: readImageData failed"));
    if (hasDecodeDiagnostic != debugEnabled)
        return fail("image decoder diagnostics did not follow the existing --debug flag");

    const QStringList sources = {
        QStringLiteral("matrix"), QStringLiteral("meshcore"),
        QStringLiteral("xmpp"), QStringLiteral("filesystem"),
        QStringLiteral("matrix"), QStringLiteral("xmpp")
    };
    QList<qint64> startTimes;
    QElapsedTimer clock;
    clock.start();
    int activeLoads = 0;
    int maximumActiveLoads = 0;
    int completedLoads = 0;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

    for (int i = 0; i < sources.size(); ++i) {
        const QString key = sources.at(i) + QStringLiteral(":image-%1").arg(i);
        scheduler->submit(key,
            [scheduler, &startTimes, &clock, &activeLoads, &maximumActiveLoads,
             &completedLoads, &loop](ImageLoadScheduler::Completion done) {
                startTimes.append(clock.elapsed());
                ++activeLoads;
                maximumActiveLoads = qMax(maximumActiveLoads, activeLoads);
                QTimer::singleShot(4200, scheduler,
                    [&activeLoads, &completedLoads, &loop, done]() {
                        --activeLoads;
                        done(QImage());
                    });
            }, &application,
            [&completedLoads, &loop](const QImage &) {
                ++completedLoads;
                if (completedLoads == 6)
                    loop.quit();
            });
    }

    timeout.start(15000);
    loop.exec();
    timeout.stop();

    if (completedLoads != sources.size())
        return fail("global image queue did not finish all admitted operations");
    if (startTimes.size() != sources.size())
        return fail("global image queue did not start every operation exactly once");
    for (int i = 1; i < startTimes.size(); ++i) {
        if (startTimes.at(i) - startTimes.at(i - 1) < 900)
            return fail("global image queue started more than one image per second");
    }
    if (maximumActiveLoads != 4)
        return fail("global image queue did not enforce a four-load concurrency ceiling");

    std::cout << "Global image queue rate and concurrency test passed\n";
    return 0;
}
