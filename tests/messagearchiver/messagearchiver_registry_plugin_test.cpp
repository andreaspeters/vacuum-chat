#include <QCoreApplication>
#include <QPluginLoader>

#include <cstdio>

#include <interfaces/iprotocolmessagingregistry.h>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: messagearchiver_registry_plugin_tests <plugin-path>\n");
        return 2;
    }

    QPluginLoader loader(QString::fromLocal8Bit(argv[1]));
    QObject *instance = loader.instance();
    if (!instance)
    {
        std::fprintf(stderr, "plugin load failed: %s\n",
            loader.errorString().toLocal8Bit().constData());
        return 1;
    }

    if (!qobject_cast<IProtocolMessagingRegistry *>(instance))
    {
        std::fprintf(stderr, "MessageArchiver does not expose IProtocolMessagingRegistry\n");
        return 1;
    }
    return 0;
}
