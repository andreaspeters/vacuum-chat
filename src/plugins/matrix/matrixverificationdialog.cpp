#include "matrixverificationdialog.h"

#include <QDialogButtonBox>
#include <QFont>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

MatrixVerificationDialog::MatrixVerificationDialog(const QString &transactionId,
    const QString &userId, const QString &deviceId, QWidget *parent)
    : QDialog(parent), FTransactionId(transactionId), FUserId(userId), FDeviceId(deviceId),
      FStatusLabel(new QLabel(this)), FEmojiLabel(new QLabel(this)),
      FDecimalLabel(new QLabel(this)), FCrossSigningLabel(new QLabel(this)),
      FAcceptButton(new QPushButton(tr("Accept"), this)),
      FStartButton(new QPushButton(tr("Start verification"), this)),
      FConfirmButton(new QPushButton(tr("The codes match"), this))
{
    setWindowTitle(tr("Matrix device verification"));
    setModal(false);
    setMinimumWidth(420);

    auto *layout = new QVBoxLayout(this);
    auto *description = new QLabel(
        tr("Compare the emoji or decimal code with the other device. Only confirm if they match exactly."),
        this);
    description->setWordWrap(true);
    layout->addWidget(description);
    layout->addWidget(FStatusLabel);

    FEmojiLabel->setAlignment(Qt::AlignCenter);
    FEmojiLabel->setWordWrap(true);
    QFont emojiFont = FEmojiLabel->font();
    emojiFont.setPointSize(20);
    FEmojiLabel->setFont(emojiFont);
    layout->addWidget(FEmojiLabel);

    FDecimalLabel->setAlignment(Qt::AlignCenter);
    QFont decimalFont = FDecimalLabel->font();
    decimalFont.setPointSize(16);
    decimalFont.setBold(true);
    FDecimalLabel->setFont(decimalFont);
    layout->addWidget(FDecimalLabel);
    FCrossSigningLabel->setWordWrap(true);
    layout->addWidget(FCrossSigningLabel);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, Qt::Horizontal, this);
    buttons->addButton(FAcceptButton, QDialogButtonBox::AcceptRole);
    buttons->addButton(FStartButton, QDialogButtonBox::AcceptRole);
    buttons->addButton(FConfirmButton, QDialogButtonBox::AcceptRole);
    layout->addWidget(buttons);

    connect(FAcceptButton, &QPushButton::clicked, this, [this]() {
        emit acceptRequested(FTransactionId, FUserId, FDeviceId);
    });
    connect(FStartButton, &QPushButton::clicked, this, [this]() {
        emit startRequested(FTransactionId, FUserId, FDeviceId);
    });
    connect(FConfirmButton, &QPushButton::clicked, this, [this]() {
        emit confirmRequested(FTransactionId, FUserId, FDeviceId);
    });
    connect(buttons, &QDialogButtonBox::rejected, this, [this]() {
        emit cancelRequested(FTransactionId, FUserId, FDeviceId);
        reject();
    });

    setSas(QStringList(), QString());
    setCrossSigningStatus(false);
    setState(QStringLiteral("requested"));
}

void MatrixVerificationDialog::setCrossSigningStatus(bool complete)
{
    FCrossSigningLabel->setText(complete
        ? tr("Cross-signing chain: master, self-signing and user-signing keys available.")
        : tr("Cross-signing chain is not complete. SAS verification remains the direct device trust method."));
}

void MatrixVerificationDialog::setState(const QString &state)
{
    FState = state;
    QString status;
    if (state == QStringLiteral("requested"))
        status = tr("The other device requests verification.");
    else if (state == QStringLiteral("ready"))
        status = tr("Verification is ready to start.");
    else if (state == QStringLiteral("started") || state == QStringLiteral("accepted") ||
             state == QStringLiteral("key_sent") || state == QStringLiteral("key_received"))
        status = tr("Waiting for both devices to exchange keys.");
    else if (state == QStringLiteral("mac_sent"))
        status = tr("Waiting for the other device to confirm.");
    else if (state == QStringLiteral("verified"))
        status = tr("Device verified successfully.");
    else if (state.startsWith(QStringLiteral("cancelled")))
        status = tr("Verification cancelled.");
    else
        status = state;
    FStatusLabel->setText(status);
    updateButtons();
}

void MatrixVerificationDialog::setSas(const QStringList &emoji, const QString &decimal)
{
    FEmojiLabel->setText(emoji.isEmpty() ? QString() : emoji.join(QStringLiteral("  ")));
    FDecimalLabel->setText(decimal.isEmpty() ? QString() : tr("Decimal: %1").arg(decimal));
    updateButtons();
}

void MatrixVerificationDialog::updateButtons()
{
    FAcceptButton->setVisible(FState == QStringLiteral("requested"));
    FStartButton->setVisible(FState == QStringLiteral("ready"));
    FConfirmButton->setVisible(!FEmojiLabel->text().isEmpty() &&
                               (FState == QStringLiteral("key_received") ||
                                FState == QStringLiteral("mac_verified")));
    const bool finished = FState == QStringLiteral("verified") || FState.startsWith(QStringLiteral("cancelled"));
    FAcceptButton->setEnabled(!finished);
    FStartButton->setEnabled(!finished);
    FConfirmButton->setEnabled(!finished);
}
