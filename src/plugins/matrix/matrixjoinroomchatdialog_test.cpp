#include "matrixjoinroomchatdialog.h"

#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>

#include <iostream>

namespace {
int fail(const char *message)
{
    std::cerr << message << '\n';
    return 1;
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    MatrixJoinRoomChatDialog dialog;

    bool searchRequested = false;
    QString requestedServer;
    QString requestedTerm;
    int requestedLimit = 0;
    QObject::connect(&dialog, &MatrixJoinRoomChatDialog::publicRoomSearchRequested,
        [&](const QString &server, const QString &term, int limit, const QString &since) {
            searchRequested = since.isEmpty();
            requestedServer = server;
            requestedTerm = term;
            requestedLimit = limit;
        });
    auto *server = dialog.findChild<QLineEdit *>(QStringLiteral("directoryServerInput"));
    auto *term = dialog.findChild<QLineEdit *>(QStringLiteral("searchTermInput"));
    auto *search = dialog.findChild<QPushButton *>(QStringLiteral("searchButton"));
    if (!server || !term || !search)
        return fail("room-search controls are missing");
    server->setText(QStringLiteral("matrix.example.org"));
    term->setText(QStringLiteral("retro chat"));
    search->click();
    if (!searchRequested || requestedServer != QStringLiteral("matrix.example.org") ||
        requestedTerm != QStringLiteral("retro chat") || requestedLimit != 25)
        return fail("room search did not forward directory, term, limit, and initial cursor");

    MatrixPublicRooms::Room room;
    room.roomId = QStringLiteral("!retro:matrix.example.org");
    room.canonicalAlias = QStringLiteral("#retro:matrix.example.org");
    room.name = QStringLiteral("Retro Chat");
    room.topic = QStringLiteral("Public room fixture");
    room.joinedMemberCount = 7;
    MatrixPublicRooms::Result result;
    result.rooms.append(room);
    result.nextBatch = QStringLiteral("cursor-2");
    dialog.setPublicRoomsResult(result);
    auto *rooms = dialog.findChild<QListWidget *>(QStringLiteral("publicRoomsList"));
    auto *joinSelected = dialog.findChild<QPushButton *>(QStringLiteral("joinSelectedButton"));
    if (!rooms || !joinSelected || rooms->count() != 1 ||
        rooms->item(0)->data(Qt::UserRole).toString() != room.roomId)
        return fail("public-room search result was not rendered with its room ID");
    rooms->setCurrentRow(0);
    bool joinRequested = false;
    QString requestedRoom;
    QObject::connect(&dialog, &MatrixJoinRoomChatDialog::joinRoomRequested,
        [&](const QString &roomId) { joinRequested = true; requestedRoom = roomId; });
    joinSelected->click();
    if (!joinRequested || requestedRoom != room.roomId)
        return fail("selected public room did not request joining its Matrix room ID");

    auto *userId = dialog.findChild<QLineEdit *>(QStringLiteral("directUserIdInput"));
    auto *startDirect = dialog.findChild<QPushButton *>(QStringLiteral("startDirectChatButton"));
    if (!userId || !startDirect)
        return fail("direct-chat controls are missing");
    userId->setText(QStringLiteral("@alice:example.org"));
    bool directRequested = false;
    QString requestedUser;
    QObject::connect(&dialog, &MatrixJoinRoomChatDialog::startDirectChatRequested,
        [&](const QString &value) { directRequested = true; requestedUser = value; });
    startDirect->click();
    if (!directRequested || requestedUser != QStringLiteral("@alice:example.org"))
        return fail("direct-chat action did not forward a valid Matrix user ID");

    bool conversationReady = false;
    QString readyRoomId;
    QObject::connect(&dialog, &MatrixJoinRoomChatDialog::conversationReady,
        [&](const QString &roomId) { conversationReady = true; readyRoomId = roomId; });
    dialog.setDirectRoomCreated(QStringLiteral("@alice:example.org"),
        QStringLiteral("!dm:example.org"), QString());
    if (!conversationReady || readyRoomId != QStringLiteral("!dm:example.org"))
        return fail("successful direct-room creation did not expose the room conversation ID");

    std::cout << "Matrix join/direct-chat dialog test passed\n";
    return 0;
}
