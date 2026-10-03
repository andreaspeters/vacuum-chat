#ifndef MATRIXJOINROOMCHATDIALOG_H
#define MATRIXJOINROOMCHATDIALOG_H

#include <QDialog>
#include <QString>
#include "matrixpublicrooms.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

class MatrixJoinRoomChatDialog : public QDialog
{
    Q_OBJECT
public:
    explicit MatrixJoinRoomChatDialog(QWidget *parent = nullptr);

public slots:
    void setPublicRoomsResult(const MatrixPublicRooms::Result &result);
    void setDirectRoomCreated(const QString &userId, const QString &roomId, const QString &error);

signals:
    void publicRoomSearchRequested(const QString &directoryServer, const QString &searchTerm,
        int limit, const QString &since);
    void joinRoomRequested(const QString &roomIdOrAlias);
    void startDirectChatRequested(const QString &userId);
    void conversationReady(const QString &roomId);

private slots:
    void searchPublicRooms();
    void loadMorePublicRooms();
    void joinSelectedRoom();
    void joinEnteredRoom();
    void startDirectChat();

private:
    void requestRoomSearch(const QString &since);
    void setStatus(QLabel *label, const QString &text, bool isError = false);

    QLineEdit *FDirectoryServer = nullptr;
    QLineEdit *FSearchTerm = nullptr;
    QListWidget *FPublicRooms = nullptr;
    QPushButton *FLoadMoreButton = nullptr;
    QLabel *FRoomStatus = nullptr;
    QLineEdit *FRoomIdOrAlias = nullptr;
    QLineEdit *FDirectUserId = nullptr;
    QPushButton *FStartDirectButton = nullptr;
    QLabel *FDirectStatus = nullptr;
    QString FNextBatch;
    QString FPendingDirectUserId;
    bool FAppendResults = false;
};

#endif // MATRIXJOINROOMCHATDIALOG_H
