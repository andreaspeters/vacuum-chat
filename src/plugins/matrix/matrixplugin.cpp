#include "matrixplugin.h"
#include "matrix.h"
#include "matrixoptions.h"
#include <interfaces/ioptionsmanager.h>

MatrixPlugin::MatrixPlugin()
    : QObject(nullptr)
    , FMatrix(nullptr)
{
}

MatrixPlugin::~MatrixPlugin()
{
}

QObject *MatrixPlugin::instance()
{
    return FMatrix;
}

QUuid MatrixPlugin::pluginUuid() const
{
    // Unique UUID for the Matrix plugin
    return QUuid("{89de35ee-bd44-49fc-8495-edd2cfebb685}");
}

void MatrixPlugin::pluginInfo(IPluginInfo *APluginInfo)
{
    APluginInfo->name = tr("Matrix");
    APluginInfo->description = tr("Matrix Client-Server API support for messaging");
    APluginInfo->version = "1.0.0";
    APluginInfo->author = "Hermes Agent";
    APluginInfo->homePage = QUrl();
    // No hard dependencies in plugin_info for Matrix
}

bool MatrixPlugin::initConnections(IPluginManager *APluginManager, int &AInitOrder)
{
    Q_UNUSED(AInitOrder);

    // Register Matrix options manager with IOptionsManager
    IOptionsManager *optionsManager = qobject_cast<IOptionsManager *>(
        APluginManager->pluginInterface("IOptionsManager").value(0));
    if (optionsManager) {
        optionsManager->insertOptionsHolder(FMatrix->optionsManager());
    }

    return true;
}

bool MatrixPlugin::initObjects()
{
    FMatrix = new Matrix(this);
    return true;
}

bool MatrixPlugin::initSettings()
{
    return true;
}

bool MatrixPlugin::startPlugin()
{
    // Start network operations
    FMatrix->start();

    return true;
}