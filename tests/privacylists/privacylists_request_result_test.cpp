#include <QCoreApplication>
#include <QPluginLoader>

#include <cstdio>

#include <interfaces/istanzaprocessor.h>

int main(int argc, char *argv[])
{
	QCoreApplication app(argc, argv);
	if (argc != 2)
	{
		std::fprintf(stderr, "usage: privacylists_request_result_test <plugin-path>\n");
		return 2;
	}

	QPluginLoader loader(QString::fromLocal8Bit(argv[1]));
	QObject *plugin = loader.instance();
	if (!plugin)
	{
		std::fprintf(stderr, "failed to load PrivacyLists plugin: %s\n", qPrintable(loader.errorString()));
		return 3;
	}

	IStanzaRequestOwner *requestOwner = qobject_cast<IStanzaRequestOwner *>(plugin);
	if (!requestOwner)
	{
		std::fprintf(stderr, "PrivacyLists plugin does not expose IStanzaRequestOwner\n");
		return 4;
	}

	Stanza response("iq");
	response.setType("result").setId("synthetic-untracked-request");
	requestOwner->stanzaRequestResult(Jid("test@example.invalid"), response);

	if (!loader.unload())
	{
		std::fprintf(stderr, "failed to unload PrivacyLists plugin: %s\n", qPrintable(loader.errorString()));
		return 5;
	}
	return 0;
}
