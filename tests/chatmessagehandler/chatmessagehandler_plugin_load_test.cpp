#include <QApplication>
#include <QPluginLoader>

#include <cstdio>

int main(int argc, char *argv[])
{
	QApplication app(argc, argv);
	if (argc != 2)
	{
		std::fprintf(stderr, "usage: chatmessagehandler_plugin_load_tests <plugin-path>\n");
		return 2;
	}

	QPluginLoader loader(QString::fromLocal8Bit(argv[1]));
	if (!loader.instance())
	{
		std::fprintf(stderr, "plugin load failed: %s\n",
			loader.errorString().toLocal8Bit().constData());
		return 1;
	}
	return 0;
}
