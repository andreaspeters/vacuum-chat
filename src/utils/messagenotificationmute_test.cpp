#include "messagenotificationmute.h"

#include <QCoreApplication>
#include <QDomDocument>
#include <iostream>

namespace
{
int failures = 0;

void expect(bool condition, const char *description)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << description << '\n';
		++failures;
	}
}

bool setDocumentContent(QDomDocument &document, const QByteArray &xml)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
	return static_cast<bool>(document.setContent(xml));
#else
	QString error;
	int line = 0;
	int column = 0;
	return document.setContent(xml, &error, &line, &column);
#endif
}

void resetOptions()
{
	QDomDocument options;
	options.appendChild(options.createElement("options"));
	Options::setOptions(options, QString(), QByteArray());
}

QDomDocument exportedOptions()
{
	QDomDocument options;
	QDomElement root = options.createElement("options");
	options.appendChild(root);
	Options::exportNode(OPV_MESSAGES_MUTED_TARGETS, root);
	return options;
}

bool containsTarget(const QDomDocument &options, const QString &key)
{
	const QDomElement messages = options.documentElement().firstChildElement("messages");
	const QDomElement targets = messages.firstChildElement("muted-notification-targets");
	for (QDomElement target = targets.firstChildElement("target"); !target.isNull();
		target = target.nextSiblingElement("target"))
	{
		if (target.attribute("ns") == key && target.text() == "true")
			return true;
	}
	return false;
}

void testUnmutedLookupDoesNotCreateOptions()
{
	resetOptions();
	const QString streamId = QStringLiteral("synthetic-account");
	const QString targetId = QStringLiteral("synthetic-room");

	expect(!Options::hasNode(OPV_MESSAGES_MUTED_TARGETS), "mute list starts absent");
	expect(!messageNotificationMuted(streamId, targetId), "unknown room is not muted");
	expect(!Options::hasNode(OPV_MESSAGES_MUTED_TARGETS),
		"checking an unmuted room does not create persisted options");
}

void testMuteUsesXmlSafeNamespaceAndUnmuteRemovesIt()
{
	resetOptions();
	const QString streamId = QStringLiteral("synthetic-account");
	QString targetId;
	QString key;
	for (int i = 0; i < 1000; ++i)
	{
		targetId = QStringLiteral("synthetic-room-%1").arg(i);
		key = messageNotificationMuteKey(streamId, targetId);
		if (!key.isEmpty() && key.at(0).isDigit())
			break;
	}
	expect(!key.isEmpty() && key.at(0).isDigit(), "fixture exercises a digit-leading hash");
	if (key.isEmpty() || !key.at(0).isDigit())
		return;

	setMessageNotificationMuted(streamId, targetId, true);
	expect(messageNotificationMuted(streamId, targetId), "muted room can be queried");

	const QDomDocument stored = exportedOptions();
	expect(containsTarget(stored, key), "muted room is stored under target/@ns");
	QDomDocument parsed;
	expect(setDocumentContent(parsed, stored.toByteArray()),
		"mute options serialize as well-formed XML");

	setMessageNotificationMuted(streamId, targetId, false);
	expect(!messageNotificationMuted(streamId, targetId), "unmuted room queries false");
	expect(!Options::hasNode(QString::fromLatin1(OPV_MESSAGES_MUTED_TARGETS) + ".target", key),
		"unmuting removes the namespaced room entry");
}

void testLegacyXmlMigrationPreservesAccountAndOnlyMutedTargets()
{
	const QString mutedHash(64, QLatin1Char('1'));
	const QString falseHash(64, QLatin1Char('2'));
	const QString emptyHash(64, QLatin1Char('3'));
	const QString letterHash(64, QLatin1Char('a'));
	QByteArray legacy = QByteArrayLiteral(
		"<options><accounts><account ns=\"synthetic-account\"><type type=\"10\">meshcore</type>"
		"<active type=\"1\">true</active></account></accounts><messages>"
		"<muted-notification-targets>");
	legacy += '<' + mutedHash.toLatin1() + QByteArrayLiteral(" type=\"1\">true</")
		+ mutedHash.toLatin1() + QByteArrayLiteral(">");
	legacy += '<' + falseHash.toLatin1() + QByteArrayLiteral(" type=\"1\">false</")
		+ falseHash.toLatin1() + QByteArrayLiteral(">");
	legacy += '<' + emptyHash.toLatin1() + QByteArrayLiteral("/>");
	legacy += '<' + letterHash.toLatin1() + QByteArrayLiteral(" type=\"1\">false</")
		+ letterHash.toLatin1() + QByteArrayLiteral(">");
	legacy += QByteArrayLiteral("</muted-notification-targets></messages></options>");

	QDomDocument rawDocument;
	expect(!setDocumentContent(rawDocument, legacy),
		"legacy digit-leading tag names reproduce the XML parse failure");

	const QByteArray migrated = migrateLegacyMessageNotificationMuteOptionsXml(legacy);
	expect(migrated != legacy, "legacy mute data is transformed before parsing");
	QDomDocument document;
	expect(setDocumentContent(document, migrated), "migrated options XML parses successfully");
	if (document.isNull())
		return;

	const QDomElement messages = document.documentElement().firstChildElement("messages");
	const QDomElement targets = messages.firstChildElement("muted-notification-targets");
	int targetCount = 0;
	bool retainedMutedTarget = false;
	for (QDomElement target = targets.firstChildElement("target"); !target.isNull();
		target = target.nextSiblingElement("target"))
	{
		++targetCount;
		retainedMutedTarget = retainedMutedTarget ||
			(target.attribute("ns") == mutedHash && target.text() == "true");
	}
	expect(targetCount == 1, "migration retains only explicitly muted targets");
	expect(retainedMutedTarget, "migration retains the true mute entry");
	bool legacyEntryRemains = false;
	for (QDomElement child = targets.firstChildElement(); !child.isNull(); child = child.nextSiblingElement())
		legacyEntryRemains = legacyEntryRemains || child.tagName() != "target";
	expect(!legacyEntryRemains, "migration removes legacy hash-named entries");

	const QDomElement account = document.documentElement().firstChildElement("accounts")
		.firstChildElement("account");
	expect(!account.isNull() && account.attribute("ns") == "synthetic-account",
		"migration preserves the MeshCore account node");
	expect(account.firstChildElement("type").text() == "meshcore" &&
		account.firstChildElement("active").text() == "true",
		"migration preserves account settings");
}
}

int main(int argc, char *argv[])
{
	QCoreApplication app(argc, argv);
	testUnmutedLookupDoesNotCreateOptions();
	testMuteUsesXmlSafeNamespaceAndUnmuteRemovesIt();
	testLegacyXmlMigrationPreservesAccountAndOnlyMutedTargets();
	if (failures != 0)
		return 1;
	std::cout << "message notification mute tests passed\n";
	return 0;
}
