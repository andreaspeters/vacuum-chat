#include "matrixroominvite.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <iostream>

namespace {
int fail(const char *message)
{
    std::cerr << message << '\n';
    return 1;
}
}

int main()
{
    const QString roomId = QStringLiteral("!room:example.org");
    const QString userId = QStringLiteral("@invitee:example.org");
    MatrixRoomInvite::Request request;
    if (!MatrixRoomInvite::buildRequest(roomId, userId, request))
        return fail("valid Matrix room invite request was rejected");
    if (request.path != QStringLiteral("/_matrix/client/v3/rooms/%21room%3Aexample.org/invite"))
        return fail("Matrix room invite request used the wrong endpoint path");
    const QJsonObject body = QJsonDocument::fromJson(request.body).object();
    if (body.value(QStringLiteral("user_id")).toString() != userId || body.size() != 1)
        return fail("Matrix room invite request has the wrong JSON body");

    MatrixRoomInvite::Request invalid;
    if (MatrixRoomInvite::buildRequest(QStringLiteral("room:example.org"), userId, invalid))
        return fail("invite request accepted a room alias in place of a room ID");
    if (MatrixRoomInvite::buildRequest(roomId, QStringLiteral("not-a-user-id"), invalid))
        return fail("invite request accepted a malformed Matrix user ID");

    std::cout << "Matrix room-invite request test passed\n";
    return 0;
}
