#ifndef MATRIXOPTIONSWIDGET_H
#define MATRIXOPTIONSWIDGET_H

#include <QWidget>

namespace Ui {
class MatrixOptionsWidget;
}

class MatrixOptionsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MatrixOptionsWidget(QWidget *AParent = nullptr);
    ~MatrixOptionsWidget();

    QString homeserver() const;
    void setHomeserver(const QString &AHomeserver);

    void load();
    void save();

signals:
    void modified();

private:
    Ui::MatrixOptionsWidget *ui;
};

#endif // MATRIXOPTIONSWIDGET_H