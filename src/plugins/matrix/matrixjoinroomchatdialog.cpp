#include "matrixjoinroomchatdialog.h"

#include "matrixdirectroom.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

MatrixJoinRoomChatDialog::MatrixJoinRoomChatDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Join Matrix room or start direct chat"));
    setMinimumSize(560, 420);

    auto *layout = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
    auto *roomsPage = new QWidget(tabs);
    auto *roomsLayout = new QVBoxLayout(roomsPage);
    auto *searchForm = new QFormLayout();
    FDirectoryServer = new QLineEdit(roomsPage);
    FDirectoryServer->setObjectName(QStringLiteral("directoryServerInput"));
    FDirectoryServer->setPlaceholderText(tr("Optional; defaults to your homeserver"));
    FSearchTerm = new QLineEdit(roomsPage);
    FSearchTerm->setObjectName(QStringLiteral("searchTermInput"));
    searchForm->addRow(tr("Directory server:"), FDirectoryServer);
    searchForm->addRow(tr("Search:"), FSearchTerm);
    roomsLayout->addLayout(searchForm);

    auto *searchButtons = new QHBoxLayout();
    auto *searchButton = new QPushButton(tr("Search public rooms"), roomsPage);
    searchButton->setObjectName(QStringLiteral("searchButton"));
    FLoadMoreButton = new QPushButton(tr("Load more"), roomsPage);
    FLoadMoreButton->setObjectName(QStringLiteral("loadMoreButton"));
    FLoadMoreButton->setEnabled(false);
    searchButtons->addWidget(searchButton);
    searchButtons->addWidget(FLoadMoreButton);
    searchButtons->addStretch();
    roomsLayout->addLayout(searchButtons);

    FPublicRooms = new QListWidget(roomsPage);
    FPublicRooms->setObjectName(QStringLiteral("publicRoomsList"));
    roomsLayout->addWidget(FPublicRooms, 1);
    auto *joinSelected = new QPushButton(tr("Join selected room"), roomsPage);
    joinSelected->setObjectName(QStringLiteral("joinSelectedButton"));
    joinSelected->setEnabled(false);
    roomsLayout->addWidget(joinSelected, 0, Qt::AlignRight);

    auto *joinForm = new QFormLayout();
    FRoomIdOrAlias = new QLineEdit(roomsPage);
    FRoomIdOrAlias->setObjectName(QStringLiteral("roomIdOrAliasInput"));
    FRoomIdOrAlias->setPlaceholderText(tr("!room:server or #alias:server"));
    auto *joinEntered = new QPushButton(tr("Join"), roomsPage);
    joinEntered->setObjectName(QStringLiteral("joinRoomButton"));
    auto *roomEntry = new QWidget(roomsPage);
    auto *roomEntryLayout = new QHBoxLayout(roomEntry);
    roomEntryLayout->setContentsMargins(0, 0, 0, 0);
    roomEntryLayout->addWidget(FRoomIdOrAlias, 1);
    roomEntryLayout->addWidget(joinEntered);
    joinForm->addRow(tr("Room ID or alias:"), roomEntry);
    roomsLayout->addLayout(joinForm);
    FRoomStatus = new QLabel(roomsPage);
    FRoomStatus->setObjectName(QStringLiteral("roomStatusLabel"));
    FRoomStatus->setWordWrap(true);
    roomsLayout->addWidget(FRoomStatus);
    tabs->addTab(roomsPage, tr("Rooms"));

    auto *directPage = new QWidget(tabs);
    auto *directLayout = new QVBoxLayout(directPage);
    auto *directForm = new QFormLayout();
    FDirectUserId = new QLineEdit(directPage);
    FDirectUserId->setObjectName(QStringLiteral("directUserIdInput"));
    FDirectUserId->setPlaceholderText(tr("@user:server"));
    directForm->addRow(tr("Matrix user ID:"), FDirectUserId);
    directLayout->addLayout(directForm);
    FStartDirectButton = new QPushButton(tr("Start direct chat"), directPage);
    FStartDirectButton->setObjectName(QStringLiteral("startDirectChatButton"));
    directLayout->addWidget(FStartDirectButton, 0, Qt::AlignLeft);
    FDirectStatus = new QLabel(directPage);
    FDirectStatus->setObjectName(QStringLiteral("directStatusLabel"));
    FDirectStatus->setWordWrap(true);
    directLayout->addWidget(FDirectStatus);
    directLayout->addStretch();
    tabs->addTab(directPage, tr("Direct chat"));
    layout->addWidget(tabs, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(searchButton, &QPushButton::clicked, this, &MatrixJoinRoomChatDialog::searchPublicRooms);
    connect(FSearchTerm, &QLineEdit::returnPressed, this, &MatrixJoinRoomChatDialog::searchPublicRooms);
    connect(FLoadMoreButton, &QPushButton::clicked, this, &MatrixJoinRoomChatDialog::loadMorePublicRooms);
    connect(joinSelected, &QPushButton::clicked, this, &MatrixJoinRoomChatDialog::joinSelectedRoom);
    connect(joinEntered, &QPushButton::clicked, this, &MatrixJoinRoomChatDialog::joinEnteredRoom);
    connect(FRoomIdOrAlias, &QLineEdit::returnPressed, this, &MatrixJoinRoomChatDialog::joinEnteredRoom);
    connect(FPublicRooms, &QListWidget::currentRowChanged, this,
        [joinSelected, this](int row) { joinSelected->setEnabled(row >= 0 && FPublicRooms->item(row)); });
    connect(FPublicRooms, &QListWidget::itemDoubleClicked, this,
        [this](QListWidgetItem *) { joinSelectedRoom(); });
    connect(FStartDirectButton, &QPushButton::clicked, this, &MatrixJoinRoomChatDialog::startDirectChat);
    connect(FDirectUserId, &QLineEdit::returnPressed, this, &MatrixJoinRoomChatDialog::startDirectChat);
}

void MatrixJoinRoomChatDialog::setPublicRoomsResult(const MatrixPublicRooms::Result &result)
{
    if (!result.error.isEmpty()) {
        FNextBatch.clear();
        FLoadMoreButton->setEnabled(false);
        setStatus(FRoomStatus, result.error, true);
        FAppendResults = false;
        return;
    }
    if (!FAppendResults)
        FPublicRooms->clear();

    int added = 0;
    for (const MatrixPublicRooms::Room &room : result.rooms) {
        if (room.roomId.isEmpty())
            continue;
        bool duplicate = false;
        for (int i = 0; i < FPublicRooms->count(); ++i)
            if (FPublicRooms->item(i)->data(Qt::UserRole).toString() == room.roomId) {
                duplicate = true;
                break;
            }
        if (duplicate)
            continue;

        QString title = room.name.trimmed();
        if (title.isEmpty())
            title = room.canonicalAlias.isEmpty() ? room.roomId : room.canonicalAlias;
        QString details = room.canonicalAlias;
        if (details.isEmpty() || details == title)
            details = room.roomId;
        const QString text = tr("%1 — %2 members\n%3")
            .arg(title).arg(room.joinedMemberCount).arg(details);
        auto *item = new QListWidgetItem(text, FPublicRooms);
        item->setData(Qt::UserRole, room.roomId);
        item->setToolTip(room.topic.isEmpty() ? room.roomId : room.topic + QStringLiteral("\n") + room.roomId);
        ++added;
    }
    FNextBatch = result.nextBatch;
    FLoadMoreButton->setEnabled(!FNextBatch.isEmpty());
    FAppendResults = false;
    setStatus(FRoomStatus, tr("%1 public room(s) shown").arg(FPublicRooms->count()));
    if (added == 0 && FPublicRooms->count() == 0)
        setStatus(FRoomStatus, tr("No public rooms found"));
}

void MatrixJoinRoomChatDialog::setDirectRoomCreated(const QString &userId, const QString &roomId,
    const QString &error)
{
    if (userId != FPendingDirectUserId || FPendingDirectUserId.isEmpty())
        return;
    FPendingDirectUserId.clear();
    FStartDirectButton->setEnabled(true);
    if (roomId.isEmpty()) {
        setStatus(FDirectStatus, error.isEmpty() ? tr("Could not create direct room") : error, true);
        return;
    }
    if (!error.isEmpty())
        setStatus(FDirectStatus, error, true);
    else
        setStatus(FDirectStatus, tr("Direct room created; it will appear in your room list."));
    emit conversationReady(roomId);
}

void MatrixJoinRoomChatDialog::searchPublicRooms()
{
    FAppendResults = false;
    FNextBatch.clear();
    FPublicRooms->clear();
    FLoadMoreButton->setEnabled(false);
    setStatus(FRoomStatus, tr("Searching public rooms…"));
    requestRoomSearch(QString());
}

void MatrixJoinRoomChatDialog::loadMorePublicRooms()
{
    if (FNextBatch.isEmpty())
        return;
    FAppendResults = true;
    FLoadMoreButton->setEnabled(false);
    setStatus(FRoomStatus, tr("Loading more public rooms…"));
    requestRoomSearch(FNextBatch);
}

void MatrixJoinRoomChatDialog::joinSelectedRoom()
{
    QListWidgetItem *item = FPublicRooms->currentItem();
    if (!item)
        return;
    const QString roomId = item->data(Qt::UserRole).toString();
    if (roomId.isEmpty())
        return;
    emit joinRoomRequested(roomId);
    setStatus(FRoomStatus, tr("Join requested for %1; the room will appear after sync.").arg(roomId));
}

void MatrixJoinRoomChatDialog::joinEnteredRoom()
{
    const QString roomIdOrAlias = FRoomIdOrAlias->text().trimmed();
    if (roomIdOrAlias.isEmpty()) {
        setStatus(FRoomStatus, tr("Enter a Matrix room ID or alias."), true);
        return;
    }
    emit joinRoomRequested(roomIdOrAlias);
    setStatus(FRoomStatus, tr("Join requested for %1; the room will appear after sync.").arg(roomIdOrAlias));
}

void MatrixJoinRoomChatDialog::startDirectChat()
{
    const QString userId = FDirectUserId->text().trimmed();
    if (!MatrixDirectRoom::isValidUserId(userId)) {
        setStatus(FDirectStatus, tr("Enter a Matrix user ID in the form @user:server."), true);
        return;
    }
    FPendingDirectUserId = userId;
    FStartDirectButton->setEnabled(false);
    setStatus(FDirectStatus, tr("Creating direct room…"));
    emit startDirectChatRequested(userId);
}

void MatrixJoinRoomChatDialog::requestRoomSearch(const QString &since)
{
    emit publicRoomSearchRequested(FDirectoryServer->text().trimmed(), FSearchTerm->text().trimmed(),
        25, since);
}

void MatrixJoinRoomChatDialog::setStatus(QLabel *label, const QString &text, bool isError)
{
    label->setText(text);
    label->setStyleSheet(isError ? QStringLiteral("color: #b00020;") : QString());
}
