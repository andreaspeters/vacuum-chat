#include "matrixoptions.h"
#include "matrixoptionswidget.h"
#include <interfaces/ioptionsmanager.h>
#include <interfaces/ioptionsholder.h>

MatrixOptions::MatrixOptions(QObject *AParent)
    : QObject(AParent)
    , FOptionsWidget(nullptr)
    , FOptionsManager(nullptr)
    , FModified(false)
{
}

MatrixOptions::~MatrixOptions()
{
    if (FOptionsWidget) {
        FOptionsWidget->setParent(nullptr);
    }
}

IOptionsWidget *MatrixOptions::optionsWidget()
{
    if (!FOptionsWidget) {
        FOptionsWidget = new MatrixOptionsWidget(this);
        connect(FOptionsWidget, &MatrixOptionsWidget::modified,
                this, &MatrixOptions::onWidgetModified);
    }
    return FOptionsWidget;
}

bool MatrixOptions::apply()
{
    FModified = false;
    return true;
}

void MatrixOptions::reset()
{
    if (FOptionsWidget) {
        FOptionsWidget->load();
    }
    FModified = false;
}

QString MatrixOptions::homeserver() const
{
    if (FOptionsWidget) {
        return FOptionsWidget->homeserver();
    }
    return QString();
}

void MatrixOptions::setHomeserver(const QString &AHomeserver)
{
    if (FOptionsWidget) {
        FOptionsWidget->setHomeserver(AHomeserver);
        FModified = true;
        emit optionsChanged();
    }
}

void MatrixOptions::onWidgetModified()
{
    FModified = true;
    emit optionsChanged();
}