#include "matrixssssdialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

MatrixSsssDialog::MatrixSsssDialog(QWidget *parent)
    : QDialog(parent), FStatusLabel(new QLabel(this)), FInput(new QLineEdit(this))
{
    setWindowTitle(tr("Matrix recovery key"));
    setModal(true);
    setMinimumWidth(460);
    FInput->setEchoMode(QLineEdit::Password);
    FInput->setPlaceholderText(tr("Recovery key or SSSS passphrase"));
    auto *layout = new QVBoxLayout(this);
    auto *description = new QLabel(
        tr("Enter your Matrix recovery key or SSSS passphrase. It is used only in memory and is not stored."),
        this);
    description->setWordWrap(true);
    layout->addWidget(description);
    layout->addWidget(FInput);
    layout->addWidget(FStatusLabel);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         Qt::Horizontal, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        if (!FInput->text().isEmpty())
            emit recoverySubmitted(FInput->text());
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QString MatrixSsssDialog::value() const
{
    return FInput->text();
}

void MatrixSsssDialog::setResult(bool success, const QString &error)
{
    FStatusLabel->setText(success ? tr("Recovery succeeded.") : tr("Recovery failed: %1").arg(error));
    if (success) {
        FInput->clear();
        accept();
    }
}
