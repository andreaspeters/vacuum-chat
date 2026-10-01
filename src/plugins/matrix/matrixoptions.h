#ifndef MATRIXOPTIONS_H
#define MATRIXOPTIONS_H

#include <QObject>
#include <QStringList>

class IOptionsManager;
class MatrixOptionsWidget;

class MatrixOptions : public QObject
{
    Q_OBJECT

public:
    explicit MatrixOptions(QObject *AParent = nullptr);
    ~MatrixOptions();

    QString group() const { return tr("Network"); }
    QString nodeId() const { return "matrix"; }
    int order() const { return 200; }

    IOptionsWidget *optionsWidget();

    bool apply();
    void reset();

    // Matrix-specific accessors (stored in options nodes)
    QString homeserver() const;
    void setHomeserver(const QString &AHomeserver);

 signals:
    void optionsChanged();

private slots:
    void onWidgetModified();

private:
    MatrixOptionsWidget *FOptionsWidget;
    IOptionsManager *FOptionsManager;
    bool FModified;
};

#endif // MATRIXOPTIONS_H