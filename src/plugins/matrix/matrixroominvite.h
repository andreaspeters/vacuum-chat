#ifndef MATRIXROOMINVITE_H
#define MATRIXROOMINVITE_H

#include <QByteArray>
#include <QString>

namespace MatrixRoomInvite {

struct Request
{
    QString path;
    QByteArray body;
};

bool buildRequest(const QString &roomId, const QString &userId, Request &request);

} // namespace MatrixRoomInvite

#endif // MATRIXROOMINVITE_H
