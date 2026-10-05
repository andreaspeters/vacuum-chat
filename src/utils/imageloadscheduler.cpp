#include "imageloadscheduler.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QMetaObject>
#include <QMutex>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QRunnable>

#include <atomic>
#include <limits>
#include <memory>

namespace {
const int ImageStartIntervalMs = 1000;
const int MaxConcurrentImageLoads = 4;
const int MaxCachedImageCostKiB = 64 * 1024;
const QSize MaximumDecodedImageSize(1280, 1280);

QImage readImageFile(const QString &path)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize sourceSize = reader.size();
    if (sourceSize.isValid() && (sourceSize.width() > MaximumDecodedImageSize.width() ||
        sourceSize.height() > MaximumDecodedImageSize.height()))
        reader.setScaledSize(sourceSize.scaled(MaximumDecodedImageSize, Qt::KeepAspectRatio));
    return reader.read();
}

QImage readImageData(const QByteArray &data)
{
    QBuffer buffer;
    buffer.setData(data);
    if (!buffer.open(QIODevice::ReadOnly))
        return QImage();

    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    const QSize sourceSize = reader.size();
    if (sourceSize.isValid() && (sourceSize.width() > MaximumDecodedImageSize.width() ||
        sourceSize.height() > MaximumDecodedImageSize.height()))
        reader.setScaledSize(sourceSize.scaled(MaximumDecodedImageSize, Qt::KeepAspectRatio));
    return reader.read();
}
}

ImageLoadScheduler *ImageLoadScheduler::instance()
{
    static QMutex mutex;
    static ImageLoadScheduler *scheduler = nullptr;

    mutex.lock();
    if (scheduler) {
        ImageLoadScheduler *result = scheduler;
        mutex.unlock();
        return result;
    }

    QCoreApplication *application = QCoreApplication::instance();
    if (!application) {
        mutex.unlock();
        return nullptr;
    }

    if (QThread::currentThread() == application->thread()) {
        scheduler = new ImageLoadScheduler(application);
    } else {
        QMetaObject::invokeMethod(application, [application]() {
            if (!scheduler)
                scheduler = new ImageLoadScheduler(application);
        }, Qt::BlockingQueuedConnection);
    }
    ImageLoadScheduler *result = scheduler;
    mutex.unlock();
    return result;
}

QString ImageLoadScheduler::fileKey(const QString &path)
{
    return QStringLiteral("file:") + QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

ImageLoadScheduler::ImageLoadScheduler(QObject *parent) : QObject(parent),
    FDispatchTimer(new QTimer(this)), FWorkerPool(new QThreadPool(this)),
    FNextJobId(0), FLastStartMs(0), FActiveLoads(0), FHasStarted(false)
{
    FDispatchTimer->setSingleShot(true);
    FDispatchTimer->setTimerType(Qt::PreciseTimer);
    connect(FDispatchTimer, &QTimer::timeout, this, [this]() { pumpQueue(); });
    FClock.start();
    FWorkerPool->setMaxThreadCount(MaxConcurrentImageLoads);
    FImageCache.setMaxCost(MaxCachedImageCostKiB);
}

void ImageLoadScheduler::submit(const QString &key, const Operation &operation,
                                QObject *receiver, const Completion &callback)
{
    const bool hasReceiver = receiver != nullptr;
    QPointer<QObject> receiverGuard(receiver);
    if (QThread::currentThread() == thread()) {
        submitOnThread(key, operation, receiverGuard.data(), hasReceiver, callback);
        return;
    }

    QPointer<ImageLoadScheduler> self(this);
    QMetaObject::invokeMethod(this, [self, key, operation, receiverGuard, hasReceiver, callback]() {
        if (self)
            self->submitOnThread(key, operation, receiverGuard.data(), hasReceiver, callback);
    }, Qt::QueuedConnection);
}

void ImageLoadScheduler::submitOnThread(const QString &key, const Operation &operation,
                                        QObject *receiver, bool hasReceiver,
                                        const Completion &callback)
{
    Subscriber subscriber;
    subscriber.receiver = receiver;
    subscriber.hasReceiver = hasReceiver;
    subscriber.callback = callback;

    if (key.isEmpty() || !operation) {
        deliver(subscriber, QImage());
        return;
    }

    QImage *cachedImage = FImageCache.object(key);
    if (cachedImage) {
        deliver(subscriber, *cachedImage);
        return;
    }

    QSharedPointer<Job> job = FJobs.value(key);
    if (job) {
        job->subscribers.append(subscriber);
        return;
    }

    job.reset(new Job);
    job->key = key;
    job->id = ++FNextJobId;
    job->operation = operation;
    job->subscribers.append(subscriber);
    FJobs.insert(key, job);
    FPending.enqueue(job);
    pumpQueue();
}

void ImageLoadScheduler::loadFile(const QString &path, QObject *receiver, const Completion &callback)
{
    if (path.isEmpty()) {
        if (callback)
            callback(QImage());
        return;
    }

    const QString key = fileKey(path);
    submit(key, [this, path](Completion done) {
        decodeFileAsync(path, done);
    }, receiver, callback);
}

void ImageLoadScheduler::decodeFileAsync(const QString &path, const Completion &callback)
{
    runWorker([path]() { return readImageFile(path); }, callback);
}

void ImageLoadScheduler::decodeDataAsync(const QByteArray &data, const Completion &callback)
{
    runWorker([data]() { return readImageData(data); }, callback);
}

void ImageLoadScheduler::runWorker(const std::function<QImage()> &work,
                                   const Completion &callback)
{
    QPointer<ImageLoadScheduler> self(this);
    FWorkerPool->start(QRunnable::create([self, work, callback]() {
        const QImage image = work ? work() : QImage();
        if (!self)
            return;
        QMetaObject::invokeMethod(self, [self, callback, image]() {
            if (self && callback)
                callback(image);
        }, Qt::QueuedConnection);
    }));
}

void ImageLoadScheduler::pumpQueue()
{
    if (FActiveLoads >= MaxConcurrentImageLoads || FPending.isEmpty())
        return;

    if (FHasStarted) {
        const qint64 elapsedSinceStart = FClock.elapsed() - FLastStartMs;
        const int waitMs = ImageStartIntervalMs - int(elapsedSinceStart);
        if (waitMs > 0) {
            if (!FDispatchTimer->isActive())
                FDispatchTimer->start(waitMs);
            return;
        }
    }

    if (FDispatchTimer->isActive())
        FDispatchTimer->stop();

    const QSharedPointer<Job> job = FPending.dequeue();
    if (!job)
        return;

    job->started = true;
    ++FActiveLoads;
    FLastStartMs = FClock.elapsed();
    FHasStarted = true;

    const QString key = job->key;
    const quint64 id = job->id;
    QPointer<ImageLoadScheduler> self(this);
    const std::shared_ptr<std::atomic_bool> completed(new std::atomic_bool(false));
    const Completion done = [self, key, id, completed](const QImage &image) {
        if (completed->exchange(true) || !self)
            return;
        QMetaObject::invokeMethod(self, [self, key, id, image]() {
            if (self)
                self->completeJob(key, id, image);
        }, Qt::QueuedConnection);
    };

    try {
        job->operation(done);
    } catch (...) {
        done(QImage());
    }

    if (FActiveLoads < MaxConcurrentImageLoads && !FPending.isEmpty() &&
        !FDispatchTimer->isActive())
        FDispatchTimer->start(ImageStartIntervalMs);
}

void ImageLoadScheduler::completeJob(const QString &key, quint64 id, const QImage &image)
{
    QSharedPointer<Job> job = FJobs.value(key);
    if (!job || job->id != id || !job->started)
        return;

    if (FActiveLoads > 0)
        --FActiveLoads;

    if (!image.isNull()) {
        const qint64 costKiB = (qint64(image.sizeInBytes()) + 1023) / 1024;
        if (costKiB <= FImageCache.maxCost())
            FImageCache.insert(key, new QImage(image), qMax(1, int(costKiB)));
    }

    FJobs.remove(key);
    const QList<Subscriber> subscribers = job->subscribers;
    job->subscribers.clear();
    job->operation = Operation();
    for (const Subscriber &subscriber : subscribers)
        deliver(subscriber, image);

    pumpQueue();
}

void ImageLoadScheduler::deliver(const Subscriber &subscriber, const QImage &image)
{
    if (!subscriber.callback)
        return;

    if (subscriber.hasReceiver) {
        QPointer<QObject> receiver = subscriber.receiver;
        if (!receiver)
            return;
        const Completion callback = subscriber.callback;
        QMetaObject::invokeMethod(receiver, [receiver, callback, image]() {
            if (receiver)
                callback(image);
        }, Qt::QueuedConnection);
    } else {
        subscriber.callback(image);
    }
}
