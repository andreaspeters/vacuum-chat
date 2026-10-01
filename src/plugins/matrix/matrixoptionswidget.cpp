#include "matrixoptionswidget.h"
#include "ui_matrixoptions.h"

#include <QFormLayout>

MatrixOptionsWidget::MatrixOptionsWidget(QWidget *AParent)
    : QWidget(AParent)
    , ui(new Ui::MatrixOptionsWidget)
{
    ui->setupUi(this);

    // Default to matrix.org as homeserver
    ui->lneHomeserver->setText("https://matrix.org");

    connect(ui->lneHomeserver, &QLineEdit::textChanged, this, &MatrixOptionsWidget::modified);
}

MatrixOptionsWidget::~MatrixOptionsWidget()
{
    delete ui;
}

QString MatrixOptionsWidget::homeserver() const
{
    return ui->lneHomeserver->text().trimmed();
}

void MatrixOptionsWidget::setHomeserver(const QString &AHomeserver)
{
    ui->lneHomeserver->setText(AHomeserver);
}

void MatrixOptionsWidget::load()
{
    // Placeholder: restore saved homeserver from persistent storage
    // Since storage isn't integrated yet, just use current values
}

void MatrixOptionsWidget::save()
{
    // Placeholder: save homeserver to persistent storage if needed
    modified();
}