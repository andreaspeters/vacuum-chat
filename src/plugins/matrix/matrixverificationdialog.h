#ifndef MATRIXVERIFICATIONDIALOG_H
#define MATRIXVERIFICATIONDIALOG_H

#include <QDialog>
#include <QStringList>

class QLabel;
class QPushButton;

class MatrixVerificationDialog : public QDialog
{
    Q_OBJECT
public:
    MatrixVerificationDialog(const QString &transactionId, const QString &userId,
                             const QString &deviceId, QWidget *parent = nullptr);

    QString transactionId() const { return FTransactionId; }
    QString userId() const { return FUserId; }
    void setState(const QString &state);
    void setSas(const QStringList &emoji, const QString &decimal);
    void setCrossSigningStatus(bool complete);

signals:
    void acceptRequested(const QString &transactionId, const QString &userId,
                         const QString &deviceId);
    void startRequested(const QString &transactionId, const QString &userId,
                        const QString &deviceId);
    void confirmRequested(const QString &transactionId, const QString &userId,
                          const QString &deviceId);
    void cancelRequested(const QString &transactionId, const QString &userId,
                         const QString &deviceId);

private:
    void updateButtons();

    QString FTransactionId;
    QString FUserId;
    QString FDeviceId;
    QString FState;
    QLabel *FStatusLabel;
    QLabel *FEmojiLabel;
    QLabel *FDecimalLabel;
    QLabel *FCrossSigningLabel;
    QPushButton *FAcceptButton;
    QPushButton *FStartButton;
    QPushButton *FConfirmButton;
};

#endif
