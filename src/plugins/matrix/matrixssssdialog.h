#ifndef MATRIXSSSSDIALOG_H
#define MATRIXSSSSDIALOG_H

#include <QDialog>

class QLabel;
class QLineEdit;

class MatrixSsssDialog : public QDialog
{
    Q_OBJECT
public:
    explicit MatrixSsssDialog(QWidget *parent = nullptr);
    QString value() const;
    void setResult(bool success, const QString &error);

signals:
    void recoverySubmitted(const QString &value);

private:
    QLabel *FStatusLabel;
    QLineEdit *FInput;
};

#endif
