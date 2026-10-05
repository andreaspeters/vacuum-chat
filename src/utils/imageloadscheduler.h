#ifndef IMAGELOADSCHEDULER_H
#define IMAGELOADSCHEDULER_H

#include "utilsexport.h"

#include <QByteArray>
#include <QCache>
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QSharedPointer>
#include <QString>

#include <functional>

class QThreadPool;
class QTimer;

// Process-wide admission control for image reads, decodes, and image downloads.
// On Linux this lives in the shared utils library, so protocol plugins share
// one queue, one rate limit, and one bounded decoded-image cache.
class UTILS_EXPORT ImageLoadScheduler : public QObject
{
public:
    using Completion = std::function<void(const QImage &)>;
    using Operation = std::function<void(Completion)>;

    static ImageLoadScheduler *instance();
    static QString fileKey(const QString &path);

    // Operations must invoke their completion exactly once when the image load
    // (including any network fetch and decode) finishes.
    void submit(const QString &key, const Operation &operation,
                QObject *receiver, const Completion &callback);

    // Convenience for local image files. File reads and image decoding run on
    // the scheduler's bounded worker pool and are cached/coalesced by path.
    void loadFile(const QString &path, QObject *receiver, const Completion &callback);

    // Decode helpers for use inside an already-admitted submit() operation.
    // They do not consume another scheduler slot and return on this object's
    // thread; the enclosing operation remains active until its Completion runs.
    void decodeFileAsync(const QString &path, const Completion &callback);
    void decodeDataAsync(const QByteArray &data, const Completion &callback);

private:
    struct Subscriber
    {
        QPointer<QObject> receiver;
        bool hasReceiver = false;
        Completion callback;
    };

    struct Job
    {
        QString key;
        quint64 id = 0;
        Operation operation;
        QList<Subscriber> subscribers;
        bool started = false;
    };

    explicit ImageLoadScheduler(QObject *parent = nullptr);
    void submitOnThread(const QString &key, const Operation &operation,
                        QObject *receiver, bool hasReceiver,
                        const Completion &callback);
    void pumpQueue();
    void completeJob(const QString &key, quint64 id, const QImage &image);
    void deliver(const Subscriber &subscriber, const QImage &image);
    void runWorker(const std::function<QImage()> &work, const Completion &callback);

private:
    QTimer *FDispatchTimer;
    QThreadPool *FWorkerPool;
    QElapsedTimer FClock;
    QQueue<QSharedPointer<Job> > FPending;
    QHash<QString, QSharedPointer<Job> > FJobs;
    QCache<QString, QImage> FImageCache;
    quint64 FNextJobId;
    qint64 FLastStartMs;
    int FActiveLoads;
    bool FHasStarted;
};

#endif // IMAGELOADSCHEDULER_H
