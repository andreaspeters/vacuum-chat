#include <utils/imageloadscheduler.h>

#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QList>
#include <QPointer>
#include <QSharedPointer>
#include <QStringList>
#include <QTemporaryDir>
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

    QTemporaryDir imageDirectory;
    if (!imageDirectory.isValid())
        return fail("could not create a temporary image directory");
    const QString contentDetectedImagePath = imageDirectory.filePath(
        QStringLiteral("scheduler-content-detection.bin"));
    QImage sourceImage(32, 24, QImage::Format_ARGB32_Premultiplied);
    sourceImage.fill(Qt::blue);
    if (!sourceImage.save(contentDetectedImagePath, "PNG"))
        return fail("could not write a PNG image with a non-image suffix");

    QEventLoop contentDetectionLoop;
    QPointer<QEventLoop> guardedContentDetectionLoop(&contentDetectionLoop);
    QTimer contentDetectionTimeout;
    contentDetectionTimeout.setSingleShot(true);
    QObject::connect(&contentDetectionTimeout, &QTimer::timeout,
        &contentDetectionLoop, &QEventLoop::quit);
    struct ContentDetectionResult {
        bool completed = false;
        QSize imageSize;
    };
    const QSharedPointer<ContentDetectionResult> contentDetectionResult(
        new ContentDetectionResult);
    scheduler->decodeFileAsync(contentDetectedImagePath,
        [contentDetectionResult, guardedContentDetectionLoop](const QImage &image) {
            contentDetectionResult->completed = true;
            contentDetectionResult->imageSize = image.size();
            if (guardedContentDetectionLoop)
                guardedContentDetectionLoop->quit();
        });
    contentDetectionTimeout.start(5000);
    contentDetectionLoop.exec();
    contentDetectionTimeout.stop();
    if (!contentDetectionResult->completed || contentDetectionResult->imageSize != QSize(32, 24))
        return fail("image decoder did not detect image format from cached file contents");

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
    if (!decodeResult->completed || !decodeResult->nullImage)
        return fail("invalid image data did not complete with a null image");
    const bool hasDecodeDiagnostic = capturedDebugMessages.join(QLatin1Char('\n'))
        .contains(QStringLiteral("ImageLoadScheduler: readImageData failed"));
    if (hasDecodeDiagnostic != debugEnabled)
        return fail("image decoder diagnostics did not follow the existing --debug flag");

    capturedDebugMessages.clear();
    const QString missingCachePath = imageDirectory.filePath(
        QStringLiteral("missing-avatar-cache.bin"));
    QEventLoop missingCacheLoop;
    QPointer<QEventLoop> guardedMissingCacheLoop(&missingCacheLoop);
    QTimer missingCacheTimeout;
    missingCacheTimeout.setSingleShot(true);
    QObject::connect(&missingCacheTimeout, &QTimer::timeout,
        &missingCacheLoop, &QEventLoop::quit);
    struct MissingCacheResult {
        bool completed = false;
        bool nullImage = false;
    };
    const QSharedPointer<MissingCacheResult> missingCacheResult(new MissingCacheResult);
    scheduler->decodeFileAsync(missingCachePath,
        [missingCacheResult, guardedMissingCacheLoop](const QImage &image) {
            missingCacheResult->completed = true;
            missingCacheResult->nullImage = image.isNull();
            if (guardedMissingCacheLoop)
                guardedMissingCacheLoop->quit();
        });
    missingCacheTimeout.start(5000);
    missingCacheLoop.exec();
    missingCacheTimeout.stop();
    if (!missingCacheResult->completed || !missingCacheResult->nullImage)
        return fail("missing image cache entry did not complete with a null image");
    qInstallMessageHandler(previousMessageHandler);
    if (!capturedDebugMessages.isEmpty())
        return fail("missing image cache entry should not invoke image reader diagnostics");

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
