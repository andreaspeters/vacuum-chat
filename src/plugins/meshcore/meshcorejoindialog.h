#ifndef MESHCOREJOINDIALOG_H
#define MESHCOREJOINDIALOG_H

#include <QByteArray>
#include <QDialog>
#include <QList>
#include <QPair>
#include <QString>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

class MeshCoreJoinDialog : public QDialog
{
    Q_OBJECT
public:
    explicit MeshCoreJoinDialog(QWidget *parent = nullptr);

    void setChannels(const QList<QPair<QString, QString>> &channels);
    void setAvailableChannelSlots(const QList<int> &availableSlots);
    void setContacts(const QList<QPair<QString, QString>> &contacts);
    void setChannelOperationResult(int channelIndex, bool success, const QString &error);
    void setContactOperationResult(const QString &publicKeyHex, bool success,
                                   const QString &error);

signals:
    void setChannelRequested(int channelIndex, const QString &name, const QByteArray &secret);
    void addContactRequested(const QString &publicKeyHex, const QString &name);
    void conversationReady(const QString &conversationId);

private slots:
    void configureChannel();
    void joinHashtagChannel();
    void addContact();
    void openSelectedChannel();
    void openSelectedContact();
    void channelSlotChanged(int index);

private:
    enum PendingAction { NoPendingAction, ConfigureChannel, AddContact };

    void setStatus(QLabel *label, const QString &text, bool error);
    void setPending(bool pending);

    QComboBox *FChannelIndex = nullptr;
    QLineEdit *FChannelName = nullptr;
    QLineEdit *FChannelKey = nullptr;
    QListWidget *FChannels = nullptr;
    QPushButton *FConfigureChannel = nullptr;
    QPushButton *FGenerateKey = nullptr;
    QPushButton *FOpenChannel = nullptr;
    QLabel *FChannelStatus = nullptr;
    QLineEdit *FHashtagChannelName = nullptr;
    QPushButton *FJoinHashtagChannel = nullptr;
    QLabel *FHashtagStatus = nullptr;
    QList<int> FAvailableChannelSlots;

    QLineEdit *FContactName = nullptr;
    QLineEdit *FContactPublicKey = nullptr;
    QListWidget *FContacts = nullptr;
    QPushButton *FAddContact = nullptr;
    QPushButton *FOpenContact = nullptr;
    QLabel *FContactStatus = nullptr;

    PendingAction FPendingAction = NoPendingAction;
    int FCurrentChannelIndex = 0;
    int FPendingChannelIndex = -1;
    bool FPendingHashtagChannel = false;
    QString FPendingContactPublicKey;
};

#endif // MESHCOREJOINDIALOG_H
