#ifndef AX25CHAT_TEST_HELPERS_H
#define AX25CHAT_TEST_HELPERS_H

#include "ax25kisscodec.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>

#include <cerrno>
#include <unistd.h>

namespace Ax25ChatTest
{
inline bool waitForUiFrame(QCoreApplication &application, int masterFd, int timeoutMs,
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
            for (const QByteArray &packet : packets) {
                if (Ax25Kiss::Ax25UiFrameCodec::decodeUiFrame(packet, uiFrame))
                    return true;
            }
        } else if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            return false;
        }
        QThread::msleep(1);
    }
    return false;
}
}

#endif // AX25CHAT_TEST_HELPERS_H
