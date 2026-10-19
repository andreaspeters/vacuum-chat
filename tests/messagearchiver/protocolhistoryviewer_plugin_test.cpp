#include <interfaces/iprotocolhistoryviewer.h>

#include <QCoreApplication>
#include <QPluginLoader>

#include <iostream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 2) {
        std::cerr << "Expected the messagearchiver plugin path\n";
        return 1;
    }

    QPluginLoader loader(QString::fromLocal8Bit(argv[1]));
    QObject *instance = loader.instance();
    if (!instance) {
        std::cerr << "Could not load messagearchiver plugin: "
                  << loader.errorString().toStdString() << '\n';
        return 1;
    }
    if (!qobject_cast<IProtocolHistoryViewer *>(instance)) {
        std::cerr << "messagearchiver does not expose IProtocolHistoryViewer\n";
        return 1;
    }
    if (!instance->inherits("IProtocolHistoryViewer")) {
        std::cerr << "QObject::inherits does not expose the registered history viewer interface\n";
        return 1;
    }
    return 0;
}
