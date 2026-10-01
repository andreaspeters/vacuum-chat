#include <QLibrary>
#include <QApplication>
#include <QDebug>

#include <cstdio>

#include "pluginmanager.h"

namespace
{
	bool isDebugMode(int argc, char *argv[])
	{
		for (int i = 1; i < argc; ++i)
			if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--debug"))
				return true;
		return false;
	}

	void messageHandler(QtMsgType type, const QMessageLogContext &, const QString &message)
	{
		if (type == QtInfoMsg || type == QtWarningMsg || type == QtCriticalMsg)
			return;

		const QByteArray localMessage = message.toLocal8Bit();
		std::fprintf(stderr, "%s\n", localMessage.constData());
	}
}

int main(int argc, char *argv[])
{
	if (!isDebugMode(argc, argv))
		qInstallMessageHandler(messageHandler);

	QApplication app(argc, argv);
	app.setQuitOnLastWindowClosed(false);
	app.addLibraryPath(app.applicationDirPath());
	QLibrary utils(app.applicationDirPath()+"/utils",&app);
	utils.load();
	PluginManager pm(&app);
	pm.restart();
	return app.exec();
}
