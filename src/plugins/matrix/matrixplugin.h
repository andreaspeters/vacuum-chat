#ifndef MATRIXPLUGIN_H
#define MATRIXPLUGIN_H

#include <QObject>
#include <interfaces/iplugin.h>
#include <interfaces/ipluginmanager.h>

class Matrix;
class IOptionsManager;

class MatrixPlugin : public QObject, public IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "Vacuum.Core.IPlugin/1.0" FILE "matrix.json")

    Q_INTERFACES(IPlugin)

public:
    MatrixPlugin();
    ~MatrixPlugin();

    // IPlugin interface
    QObject *instance() override;
    QUuid pluginUuid() const override;
    void pluginInfo(IPluginInfo *APluginInfo) override;
    bool initConnections(IPluginManager *APluginManager, int &AInitOrder) override;
    bool initObjects() override;
    bool initSettings() override;
    bool startPlugin() override;

private:
    Matrix *FMatrix;
};

#endif // MATRIXPLUGIN_H